// Bazarish project (c) 2026
#pragma once

#include "AudioIo.hpp"
#include "CallMedia.hpp"
#include "Client.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/I2p.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bazarish::client {

// One button of an inline keyboard attached to a message. Carries a label and
// exactly one action: a callback (sends a bot.callback when tapped) or a
// command (sends a bot.command). When both are set, the callback wins.
struct InlineButton {
    std::string text;
    std::string data;     // callback payload (bot.callback); empty if none
    std::string command;  // command name (bot.command); empty if none
};

// An inline keyboard: rows of buttons rendered under a message. Tapping a
// button sends a bot.callback or bot.command back to the message's sender.
using InlineKeyboard = std::vector<std::vector<InlineButton>>;

// Serializes an inline keyboard to its JSON wire form (the value of a message
// "keyboard" field). Exposed for the bot framework, the CLI and tests.
std::string inlineKeyboardJson(const InlineKeyboard& keyboard);

// A known contact's routing and token state, persisted by the session.
struct Contact {
    // The peer's user sealing public key (SubjectPublicKeyInfo DER, base64),
    // used to E2E-encrypt message payloads to this contact.
    std::string sealingPublicB64;
    // The peer's I2P serving destination - where delivery envelopes are routed.
    std::string dest;
    // The peer's serving sealing public key (SPKI DER, base64): delivery
    // envelopes to this contact are sealed to it (held by the peer's server).
    std::string servingSealingB64;
    // Unused one-time delivery tokens (base64) issued by the peer to us:
    // each authorizes one message into the peer's mailbox.
    std::vector<std::string> sendTokens;
    // Whether we have already issued a token batch to this peer (so they
    // can write to us). Set on the contact request or the first reply.
    bool issuedToThem = false;
    // Local display name for this contact: the alias used when adding, or the
    // name carried in the invite. Purely local - never sent to the peer and
    // never overwritten by anything the peer sends. The user may rename it, and
    // that rename is mirrored only to the account's own other devices.
    std::string displayName;
    // The peer's avatar as last received (raw PNG/JPEG bytes) and its mime; the
    // bytes live in a sealed per-contact file, only the mime rides in the JSON.
    Bytes avatar;
    std::string avatarMime;
    // Whether we have already pushed our own avatar to this peer, so it is sent
    // once on establishing the dialog (not on every sync). Reset when our avatar
    // changes, so the new one is re-broadcast.
    bool avatarSentToPeer = false;
};

// A member of a group: routing plus the one-time token pool that member issued
// to the group (we spend these to deliver to them). Mirrors the reachable half
// of a Contact, but scoped to a single group.
struct GroupMember {
    std::string sealingPublicB64;
    std::string dest;
    std::string servingSealingB64;
    std::vector<std::string> sendTokens;
    bool admin = false;
};

// A group as this client knows it: the roster (other members + their routing and
// pools), the admin set (implied by member admin flags), and the roster epoch.
// Purely client-side - the server never sees a group (see docs Groups.md).
struct Group {
    std::string name;
    std::int64_t epoch = 0;
    std::map<std::string, GroupMember> members;  // other members, by fingerprint
    bool iAmAdmin = false;
    // Base64 hashes of the token pool we last issued to this group, so we can
    // revoke it (cut off a removed member) and issue a fresh one.
    std::vector<std::string> myPoolHashes;
};

// A decrypted item pulled from the mailbox during sync.
struct IncomingMessage {
    // End-to-end content type: "text", "contact.request", "token-refill",
    // "file", ... or "unsupported" for a type this client cannot render.
    std::string contentType;
    std::string fromFingerprint;
    // Plain text for "text" and "contact.request"; empty for control types.
    std::string text;
    // Server-visible delivery class this arrived under ("content"/"contact").
    std::string deliveryClass;
    // True when this item carried bootstrap (the peer's sealing key, serving
    // server and a fresh token batch) - a new or refreshed contact.
    bool establishedContact = false;
    // The original type string when contentType == "unsupported".
    std::string rawType;
    // The sender's protocol message id (envelope "id"), used to send a
    // delivery receipt back for it.
    std::string messageId;
    // The sender's send time (envelope "sentAt", unix milliseconds). The
    // recipient orders by it and shows it as the message time, not the receive
    // time (docs-main Messages.md "Ordering and timestamps"). 0 when absent.
    std::int64_t sentAt = 0;
    // For contentType == "receipt", the message id being acknowledged; for
    // contentType == "bot.callback", the keyboard message the button belongs to.
    std::string refId;

    // For the call.* content types, the call this signal belongs to. The UI uses
    // it to accept/decline an incoming call.invite and to match later signals.
    std::string callId;

    // Inline keyboard (content types may attach one), as its JSON wire form so
    // a UI can render it without a C++ parser. Empty when there is no keyboard.
    std::string keyboardJson;
    // For contentType == "bot.command": the command name and its raw argument
    // string. For contentType == "bot.callback": the button's callback data.
    std::string commandName;
    std::string commandArgs;
    std::string callbackData;

    // When non-empty, this message belongs to a group and should be filed under
    // it instead of the one-to-one thread with the sender. For
    // contentType "group.invite" it is the new group's id (and groupName its name).
    std::string groupId;
    std::string groupName;

    // Attachment (content types "file"/"photo"/"audio"/"voice"): a
    // content-store reference and the key to decrypt it. attachmentRef is
    // empty when there is no attachment.
    std::string attachmentRef;
    std::string attachmentKeyB64;
    std::string attachmentName;
    std::string attachmentMime;
    std::uint64_t attachmentSize = 0;

    // For the "avatar"/"device.avatar" content types: the raw avatar image bytes
    // (already decoded from base64), so the UI can hand them to its avatar store
    // without a chat bubble. Empty for every other type.
    std::string avatarData;
};

// The stateful client session: a user identity plus contact and token
// A blob this client externalized for a message it sent, kept so the sender can
// unsend it (delete it from blob storage with the delete-token).
struct SentBlob {
    std::string blobUrl;
    std::string deleteToken;
};

// bookkeeping persisted under a profile directory, layered over the stateless
// Client API wrappers. This is the logic a GUI or CLI front-end drives.
//
// Token model (per Contacts.md): to let a peer write to us we generate a
// batch, register its hashes with our own server and hand the raw tokens to
// the peer; to write to a peer we spend a token the peer gave us. Contact
// bootstrap exchanges both directions over a tokenless contact request and
// its reply.
class Session {
public:
    // Creates a fresh identity and sealing key under profileDir, with no server
    // connection yet. A non-empty passphrase encrypts the private key PEMs at
    // rest (AES-256-CBC); name is a human label stored in the clear for the
    // profile picker. Use connectServer() + subscribe() to attach a server.
    static Session create(const std::filesystem::path& profileDir,
        const std::string& passphrase = {}, const std::string& name = {});
    // Creates a profile already bound to a server (convenience for the CLI and
    // tests): equivalent to create() followed by connectServer().
    static Session create(const std::filesystem::path& profileDir, const ServerEndpoint& endpoint,
        const std::string& passphrase);
    // Opens an existing session. The passphrase is required when the keys
    // were created encrypted; it is ignored for unencrypted keys.
    static Session open(const std::filesystem::path& profileDir, const std::string& passphrase = {});

    // Binds the profile to a serving server (or changes it). Rebinds the
    // transport to the new endpoint and persists it; subscribe() afterwards.
    void connectServer(const ServerEndpoint& endpoint);
    // Whether a serving server is configured (a non-empty host).
    bool isConnected() const;
    const ServerEndpoint& endpoint() const;
    // The facade the transport is currently using, as a URL (for the GUI status).
    std::string activeFacadeUrl() const;
    // The configured facade URLs (the failover list, or the single facade).
    std::vector<std::string> facadeUrls() const;

    // Exports the whole session (identity, sealing key, routing meta and
    // contacts) into a single password-encrypted file (CMS PWRI). The bundle
    // holds the keys in plain PEM internally - the password protects the file.
    void exportProfile(
        const std::filesystem::path& outFile, const std::string& password) const;
    // Imports an exported bundle into a fresh profileDir. A non-empty
    // atRestPassphrase re-encrypts the imported keys on disk.
    static void importProfile(const std::filesystem::path& bundleFile,
        const std::filesystem::path& profileDir, const std::string& password,
        const std::string& atRestPassphrase = {});

    std::string fingerprint() const;
    std::string sealingPublicB64() const;
    // The human label set at creation (may be empty).
    const std::string& displayName() const;

    // The user's own avatar (raw, already-compressed PNG/JPEG bytes) and its
    // mime type; both empty when no avatar is set.
    const Bytes& avatar() const;
    const std::string& avatarMime() const;
    // Sets the user's own avatar. The bytes must already be compressed by the UI
    // to a square image within the protocol cap (500 KB). Persists it, pushes it
    // to every established contact (best effort, no error surfaced) and self-syncs
    // it to the account's other devices. Throws if the data exceeds the cap.
    void setAvatar(const Bytes& data, const std::string& mime);

    // A contact's local display name (empty when unnamed) and its avatar bytes.
    std::string contactDisplayName(const std::string& peerFingerprint) const;
    Bytes contactAvatar(const std::string& peerFingerprint) const;
    // Renames a contact locally and mirrors the change to the account's other
    // devices (a device.contact-name self-message). No-op for an unknown contact.
    void renameContact(const std::string& peerFingerprint, const std::string& name);

    // Subscribes to the configured server for the given number of days and
    // registers this client ID. Stores the returned server card.
    void subscribe(std::int64_t days);

    // The serving server's onboarding info (message + registration links),
    // shown when a connect/subscribe is refused because this key is not
    // registered yet. Requires a configured server (facades).
    PortalInfo serverPortalInfo();

    // User-owned I2P destination (the per-user / "paid" path). Free profiles
    // route through the server's address pool and never call these.
    // ensureI2pDestination mints the permanent ("master") key the first time
    // and persists it sealed at rest, returning the stable base32 address;
    // subsequent calls are idempotent. The master never leaves the client.
    std::string ensureI2pDestination();
    // Adopts an existing user-owned master from a .dat the user already holds
    // (validated as an unencrypted Ed25519 destination), persisting it sealed at
    // rest and returning its base32. Throws if the profile already has a master
    // (a different key would change the user's address) or the blob is invalid.
    std::string loadI2pDestination(const Bytes& privateKeysDat);
    bool hasI2pDestination() const;
    // The stable base32 address (without the ".b32.i2p" suffix), or empty.
    std::string i2pAddress() const;
    // Permanently removes the user-owned master (and any transient) from this
    // profile, reverting to the shared pool address. The deleted key is gone for
    // good; enabling again later would mint a fresh, different address.
    void deleteI2pDestination();

    // Turns the per-user i2p-dest option on: requires a master in the profile
    // (generate or load one first), enables it server-side (charging a term),
    // then issues and uploads a fresh transient so the personal destination
    // comes up. Returns false if the server refused (e.g. insufficient balance).
    bool enableI2pDest(std::int64_t now);
    // Turns it off server-side: the personal destination is revoked and the user
    // falls back to their fixed pool address. The master stays in the profile so
    // re-enabling later restores the same address.
    void disableI2pDest();
    // The per-user i2p-dest status from the server (for display and decisions).
    I2pDestStatus i2pDestStatus();
    // Keeps the personal destination's transient fresh: polls the server status,
    // and if the option is active and the current transient is within
    // leadSeconds of expiry (or absent), issues a fresh transient and uploads it
    // - but only after the poll, so when another of the user's devices has
    // already renewed, this one stands down (the multi-device race). Returns
    // true if it uploaded a new transient. A no-op without a personal dest.
    bool refreshI2pTransientIfDue(std::int64_t now, std::int64_t leadSeconds);
    // Issues a fresh time-boxed transient (offline keys) from the master, valid
    // until expiresUnix - the delegation handed to the serving server to operate
    // the destination for the subscription window. Throws if the profile has no
    // user-owned destination. subscribe() calls this automatically when one
    // exists, for the subscription period.
    void renewI2pTransient(std::int64_t expiresUnix);
    // The active transient blob to hand to the serving server (empty if none).
    Bytes i2pTransient() const;
    // The active transient as I2P-base64 (the form the server feeds its I2P router).
    std::string i2pTransientBase64() const;

    // Sign-in-with-key: signs an opaque challenge issued by a service portal,
    // proving ownership of this identity's key without the key ever reaching
    // the browser. Returns a base64 token the user pastes back into the portal,
    // which verifies it (see verifyLoginBlob) and recovers this fingerprint.
    std::string signLogin(const std::string& challenge) const;

    // Sends a contact request to a peer subscribed to our own server. The
    // peer's prekey, serving server and server card are looked up on our own
    // facade; the request payload is E2E-encrypted to the peer's prekey and
    // carries a fresh token batch and our own server card so the peer can
    // reply. A client never reaches another server's facade (facade locality):
    // cross-server first contact goes through an invite (self-verifying, no
    // lookup) or, in future, a sealed federated resolve over the server link.
    void sendContactRequest(const std::string& peerFingerprint, const std::string& text);

    // A bazarish:// invite carrying our full self-verifying serving chain
    // (subscription certificate + server card). A contact can verify it and
    // reach us with no trust in any server. Requires an active subscription.
    std::string inviteUri() const;

    // Adds a contact from an invite descriptor (bazarish://invite?fp&srv&srv_key):
    // the user-signed contact card is fetched for the descriptor's fingerprint
    // and verified against it, then a contact request is sent. Returns the added
    // contact's fingerprint so the UI can surface it for out-of-band verification.
    std::string addByInvite(const std::string& inviteUri, const std::string& text);

    // Adds a contact by username (alias) on the central resolver. The resolver
    // maps the alias to a descriptor over a signed, self-verifying record (chain:
    // record -> delegated key -> hardcoded resolver root); the alias->fingerprint
    // binding is the one residual trust of the name path. Everything after it -
    // the card fetch and its certificates - is verified end-to-end. Returns the
    // resolved fingerprint so the UI can surface it for out-of-band verification
    // (the only defense against a hostile resolver). Throws if no resolver is
    // configured in this build.
    std::string addByUsername(const std::string& alias, const std::string& text);

    // Overrides the central resolver coordinate (root fingerprint + destination +
    // serving key). The shipped client bakes one in (defaultResolverCoordinate);
    // this exists for deployments that point at a different resolver and for tests.
    void setResolverCoordinate(ResolverCoordinate coordinate);

    // Emits, as JSON, the artifacts the central resolver's portal needs to claim
    // <alias> for this identity: the normalized name, this user's serving
    // destination + sealing key, and a user-signed alias certificate. The buy is
    // driven by POSTing this to the resolver's /portal/buy; the signing key never
    // leaves the client. Throws if the user has no serving destination yet.
    std::string aliasBuyArtifacts(const std::string& alias) const;

    // Sends an E2E-encrypted message to an established contact, spending one
    // of the peer's tokens. Throws if the contact is unknown or out of
    // tokens. When the contact has no reciprocal tokens from us yet (the
    // first reply), a fresh batch for the peer is registered and attached.
    // messageId, when given, is used as the protocol message id (so a delivery
    // receipt can be matched back). onAcceptedByOwnServer fires once when our
    // own server has accepted the envelope into its buffer (the "grey" state).
    // Returns true if the recipient server confirmed storage within the poll
    // window (the "yellow" state), false if it was accepted but is still being
    // delivered in the background (stays grey until a read receipt confirms it).
    bool sendMessage(const std::string& peerFingerprint, const std::string& text,
        const std::string& messageId = {},
        const std::function<void()>& onAcceptedByOwnServer = {},
        std::string* outAttemptId = nullptr);

    // Sends a file as a "file" content message: the bytes are encrypted with a
    // fresh key and uploaded to the content store; the message carries the
    // reference and key end-to-end. The server never sees the content type.
    // onUploadProgress, when set, is called as the ciphertext streams out (bytes
    // sent, total), so the sender can show real upload progress.
    // retention sets how long the encrypted blob lives on the store: a TTL
    // backstop and, optionally, a download count that reclaims it the moment that
    // many recipients have fetched it (whichever comes first). Defaulted retention
    // means the store's default TTL with no download cap.
    bool sendFile(const std::string& peerFingerprint, const std::filesystem::path& path,
        const std::string& messageId = {},
        const std::function<void()>& onAcceptedByOwnServer = {},
        std::string* outAttemptId = nullptr, const UploadProgressFn& onUploadProgress = {},
        const BlobRetention& retention = {});

    // Sends an interactive message: a "text" content message carrying an inline
    // keyboard the recipient can tap to send a bot.callback / bot.command back.
    void sendInteractive(const std::string& peerFingerprint, const std::string& text,
        const InlineKeyboard& keyboard, const std::string& messageId = {},
        const std::function<void()>& onAcceptedByOwnServer = {});

    // Sends a command invocation (content type "bot.command") to a peer: a bot
    // dispatches on the command name. args is the raw argument string.
    void sendCommand(const std::string& peerFingerprint, const std::string& command,
        const std::string& args = {}, const std::string& messageId = {},
        const std::function<void()>& onAcceptedByOwnServer = {});

    // Sends a button-press callback (content type "bot.callback") to a peer:
    // data is the tapped button's payload, refMessageId the keyboard message it
    // belongs to (so the bot can correlate the press to a prior message).
    void sendCallback(const std::string& peerFingerprint, const std::string& data,
        const std::string& refMessageId = {});

    // Edits a previously sent message in place (content type "edit"): the peer
    // replaces the message whose id is refMessageId with this text and keyboard
    // (an empty keyboard removes any buttons). Both a bot updating its own
    // keyboard message on a callback and a user revising their own line use
    // this. A client applies it only to a message the sender actually sent.
    // Delivery is tracked like a normal send (onAcceptedByOwnServer / outAttemptId
    // / return value), so the edited message's bubble can reflect the edit's own
    // delivery status instead of the original's.
    bool sendEdit(const std::string& peerFingerprint, const std::string& refMessageId,
        const std::string& text, const InlineKeyboard& keyboard = {},
        const std::function<void()>& onAcceptedByOwnServer = {},
        std::string* outAttemptId = nullptr);

    // Deletes a previously sent message for everyone (content type "delete"): the
    // peer removes the message whose id is refMessageId from its transcript, with
    // no tombstone left behind. Scoped on the receiver to a message the sender
    // actually sent, exactly like an edit. Costs one delivery token.
    void sendDelete(const std::string& peerFingerprint, const std::string& refMessageId);

    // Sends a delivery receipt (content type "receipt") acknowledging that we
    // received the message with id refMessageId. Costs one delivery token.
    void sendReceipt(const std::string& peerFingerprint, const std::string& refMessageId);

    // The outcome of a still-in-flight send, re-polled after the initial submit
    // window. status is "pending", "delivered", "failed", or "unknown" (the
    // server no longer knows the attempt - it expired or the server restarted).
    struct AttemptOutcome {
        std::string status;
        std::string errorMessage;
    };
    // Re-polls a previously submitted send by its server attempt id to resolve a
    // delivery that was still pending when the send call returned. Never throws.
    AttemptOutcome pollAttempt(const std::string& attemptId);

    // Downloads a blob attachment (from a received message) over I2P, verifies
    // and decrypts it to dest. ref is the message's base64 sealed blob pointer
    // (keyB64 is unused - the key rides inside the pointer).
    void saveAttachment(const std::string& ref, const std::string& keyB64,
        const std::filesystem::path& dest, const UploadProgressFn& onProgress = {},
        const BlobStageFn& onStage = {}, const std::atomic<bool>* cancel = nullptr);

    // Sender unsend: deletes the blob this client externalized for a message it
    // sent (gated by the stored delete-token), over I2P. Best effort - the blob
    // also reclaims via its TTL. Throws if no blob was recorded for messageId.
    void unsend(const std::string& messageId);

    // Selects the I2P tunnel privacy profile used when fetching externalized
    // large blobs over a throwaway destination. Defaults to the most private.
    void setBlobFetchPrivacy(bazarish::i2p::Privacy privacy);

    // --- Audio calls (client-to-client; signalling over E2E, media over I2P) ---

    // Lifecycle of the single call this session tracks at a time.
    enum class CallState {
        eIdle,
        eOutgoing,  // we invited; awaiting the peer's accept
        eIncoming,  // a call.invite arrived; awaiting our accept/decline
        eActive,    // media is flowing
    };

    // A snapshot of the current call for the UI / CLI.
    struct CallInfo {
        CallState state = CallState::eIdle;
        std::string callId;
        std::string peerFingerprint;
        bool video = false;  // true for a video call (audio always runs too)
        bool muted = false;
        bool cameraOn = true;  // local camera state on a video call
        std::uint64_t packetsSent = 0;
        std::uint64_t packetsReceived = 0;
    };

    // Audio device backends are injected so the core stays Qt-free: the GUI sets
    // a Qt Multimedia backend; the CLI and tests fall back to the built-in
    // synthetic backend (a tone source and a counting sink). A factory makes a
    // fresh device per call.
    using AudioSourceFactory = std::function<std::unique_ptr<AudioSource>()>;
    using AudioSinkFactory = std::function<std::unique_ptr<AudioSink>()>;
    void setAudioBackend(AudioSourceFactory sourceFactory, AudioSinkFactory sinkFactory);

    // Video device backends, injected the same way: the GUI sets a Qt camera /
    // render backend; the CLI and tests fall back to the built-in synthetic
    // backend (a moving-pattern source and a counting sink).
    using VideoSourceFactory = std::function<std::unique_ptr<VideoSource>()>;
    using VideoSinkFactory = std::function<std::unique_ptr<VideoSink>()>;
    void setVideoBackend(VideoSourceFactory sourceFactory, VideoSinkFactory sinkFactory);

    // Places an outgoing audio call to an established contact. STRICT: a working
    // I2P transport is required; without it this throws ApiError(eI2pUnavailable)
    // with a readable message and no call is placed. Builds a one-time I2P
    // datagram destination for the media and sends a call.invite (carrying that
    // destination and a fresh per-call media key) over the E2E content path.
    // Throws if the contact is unknown or a call is already in progress.
    void startAudioCall(const std::string& peerFingerprint);

    // Places an outgoing video call (audio plus VP8 video). Identical to
    // startAudioCall except the call.invite negotiates video, so both sides
    // capture and render video in addition to audio.
    void startVideoCall(const std::string& peerFingerprint);

    // Accepts the pending incoming call (its id must match). STRICT I2P as above:
    // builds our media destination, replies with call.accept and starts media.
    // A video invite is accepted as a video call.
    void acceptCall(const std::string& callId);

    // Declines the pending incoming call (sends call.decline) and clears it.
    void declineCall(const std::string& callId);

    // Ends the active or ringing call (sends call.end) and tears down media.
    void endCall();

    // Mutes/unmutes the local microphone while staying connected.
    void setCallMuted(bool muted);

    // Enables/disables the local camera on a video call while staying connected
    // (the peer's view of us freezes on the last frame while disabled).
    void setCameraEnabled(bool enabled);

    // The current call snapshot (state eIdle when there is none).
    CallInfo currentCall() const;

    // Pulls, decrypts, applies (contacts/tokens) and acks all pending items.
    std::vector<IncomingMessage> sync();

    bool hasContact(const std::string& peerFingerprint) const;
    // Fingerprints of all known contacts, for UI listing.
    std::vector<std::string> contactFingerprints() const;

    // --- Groups (client-side fan-out; the server never sees a group) ---

    // Creates a group from existing contacts (their routing is reused). Signs
    // the roster, invites each member and broadcasts our token pool. Returns the
    // new group id. Throws if a listed member is not an established contact.
    std::string createGroup(
        const std::string& name, const std::vector<std::string>& memberFingerprints);

    // Sends a text message to a group: one content delivery per member (client
    // fan-out), each spending one of that member's pool tokens. Members we have
    // no pool token for yet are skipped (their pool has not arrived).
    void sendGroupMessage(const std::string& groupId, const std::string& text);

    // Group listing and membership, for the UI / CLI.
    std::vector<std::string> groupIds() const;
    std::string groupName(const std::string& groupId) const;
    std::vector<std::string> groupMemberFingerprints(const std::string& groupId) const;
    bool isGroupAdmin(const std::string& groupId) const;

    // Adds existing contacts to a group (admin only): bumps the roster epoch,
    // invites the new members, and tells the existing members. Throws if we are
    // not an admin or a listed member is not an established contact.
    void addGroupMembers(
        const std::string& groupId, const std::vector<std::string>& memberFingerprints);
    // Removes a member from a group (admin only): bumps the epoch, broadcasts the
    // new roster, and rotates the token pool so the removed member's tokens stop
    // working (revoke our old pool, issue and distribute a fresh one).
    void removeGroupMember(const std::string& groupId, const std::string& memberFingerprint);
    // Grants or revokes a member's admin flag (admin only); broadcasts the roster.
    void setGroupAdmin(const std::string& groupId, const std::string& memberFingerprint, bool admin);
    // Leaves a group: notifies the members and drops local state.
    void leaveGroup(const std::string& groupId);

    // Authenticates a group content message's per-message signature (`gsig`).
    // `body` is the unsealed inner content; `type`/`messageId`/`groupId` are the
    // values already read from it; `members` is the known member set of that group
    // (nullptr when the group is not known locally yet - membership is then
    // deferred and only the signature's from-authenticity is enforced). Returns
    // the cryptographically verified sender fingerprint, or nullopt when the
    // message is unsigned, malformed, field-mismatched, or from a non-member - in
    // which case the caller must drop it (a member could otherwise forge another
    // member's `from`). Static + pure; exposed to unit-test the spoof rejection.
    static std::optional<std::string> authenticateGroupSender(const nlohmann::json& body,
        const std::string& type, const std::string& messageId, const std::string& groupId,
        const std::map<std::string, GroupMember>* members);

private:
    Session(std::filesystem::path profileDir, std::unique_ptr<Client> client, Key sealingKey,
        std::map<std::string, Contact> contacts);

    // Generates a token batch for ourselves: registers the hashes with our
    // server and returns the raw tokens (base64) to hand to the peer.
    std::vector<std::string> issueTokenBatch();

    // Sends a contact request to a peer whose verified routing info is
    // already known (from a lookup or an invite). Mints a reply token batch
    // and records the contact, adopting displayName as its local label (the
    // alias used or the name carried in the invite) when non-empty.
    void requestWithInfo(const std::string& peerFingerprint, const std::string& text,
        const ContactInfo& info, const std::string& displayName = {});

    // The fetch transport for card / alias-resolve frames: a fresh transient-I2P
    // dial preferred (our own server uninvolved), falling back to the own-server
    // I2P proxy when there is no I2P transport of our own or the direct dial fails. A
    // served negative (CARD_UNKNOWN / ALIAS_UNKNOWN) is authoritative and does
    // not trigger the fallback - only a transport failure does.
    FetchTransport fetchTransport() const;

    // Sends a built inner content envelope to an established contact: handles
    // the first-reply bootstrap, seals to the peer and spends one token. Returns
    // whether delivery was confirmed within the poll window (see deliver()).
    // waitForOutcome defaults to false: the call returns as soon as our own server
    // accepts the envelope (grey is instant and the single worker thread is never
    // blocked on the federation/ack round-trip), and the caller reconciles the
    // delivered/failed outcome on a later sync. (The amber state arrives via the
    // recipient's signed delivered-ack, so the old inline poll is obsolete.)
    bool sendContent(const std::string& peerFingerprint, nlohmann::json inner,
        const std::function<void()>& onAcceptedByOwnServer = {},
        std::string* outAttemptId = nullptr, bool waitForOutcome = false);

    // Sends the user-owned I2P master to the account's other devices: a
    // service content message ("device.i2p-master") sealed to our own sealing
    // key and delivered tokenlessly to our own destination, so it lands in our
    // own mailbox and every device of this account picks it up on sync and
    // persists the same master (preserving the b32 across devices). Best effort;
    // a no-op when there is no master or our routing is not known yet.
    void syncI2pMasterToSelf();

    // Mints a fresh token batch for the peer and sends it as a token-refill,
    // in response to the peer signalling a low stash (Contacts.md).
    void sendTokenRefill(const std::string& peerFingerprint);

    // Pushes our own avatar to a contact as an "avatar" service message, once,
    // when the dialog is mutually established (we have engaged with them) and we
    // can reach them. A no-op (never an error) when we have no avatar, the peer
    // is unreachable, or it was already sent. Gated on issuedToThem so an
    // un-accepted incoming request never triggers an automatic avatar reply.
    void maybeSendAvatarToContact(const std::string& peerFingerprint);
    // Sends our own avatar to the account's other devices (a device.avatar
    // self-message), sealed to our own key and delivered to our own destination.
    void syncAvatarToSelf();
    // Mirrors a contact rename to the account's other devices (device.contact-name).
    void syncContactNameToSelf(const std::string& peerFingerprint, const std::string& name);
    // Persists our own avatar bytes (sealed at rest) and records its mime in meta.
    void storeOwnAvatar(const Bytes& data, const std::string& mime);
    // Persists a contact's received avatar bytes (sealed at rest) and its mime.
    void storeContactAvatar(
        const std::string& peerFingerprint, const Bytes& data, const std::string& mime);
    // Writes a profile blob, sealed under the passphrase when the profile is
    // encrypted (the generic form behind persistI2pBlob, reused for avatars).
    void persistSealedBlob(const std::string& filename, const Bytes& blob) const;

    // --- Call helpers ---

    // Builds the one-time RAW datagram endpoint that carries the call's media (a
    // published encrypted-LS b33 destination on the embedded router, torn down
    // with the call).
    std::shared_ptr<bazarish::i2p::Endpoint> openCallMediaSession();
    // Shared body of startAudioCall/startVideoCall: builds the media destination
    // (strict I2P), sends the call.invite (negotiating video when video is true)
    // and records the outgoing-call state.
    void startCall(const std::string& peerFingerprint, bool video);
    // Wires the media engine (transport + audio/video backends + codecs) for the
    // active call against the peer's media destination and starts it.
    void startCallMedia();
    // Stops media, closes the datagram session, and resets to the idle state.
    void clearCall();
    // Sends a call.* signalling content message (E2E, content class) to a peer.
    void sendCallSignal(
        const std::string& peerFingerprint, const std::string& type, nlohmann::json extra);
    // Dispatches a decrypted call.* signal during sync(), updating call state and
    // starting/stopping media as needed. Returns the content type handled.
    void handleCallSignal(const std::string& type, const std::string& from,
        const nlohmann::json& body, IncomingMessage& message);

    // Seals a delivery envelope to the destination server's sealing key and
    // hands it to our own server, which accepts it at once (store-and-forward)
    // and federates in the background. onAcceptedByOwnServer fires once on that
    // acceptance (the "grey" delivery state). Returns true if the server
    // confirmed the recipient server stored it within the poll window (the
    // "yellow" state), false if it was accepted but is still being delivered
    // (stays grey - the server keeps retrying and a read receipt confirms it
    // later). Throws on a terminal failure. When tokenRejected is non-null, a
    // token-rejected failure (the token was already spent - e.g. a concurrent
    // group sender took it) does not throw; it sets *tokenRejected and returns
    // false so the caller can retry with another token. waitForOutcome=false
    // skips the poll entirely: it submits, fires the grey callback and returns
    // false at once (used for large sends so the upload never adds a poll wait on
    // top; the caller reconciles the outcome on a later sync).
    bool deliver(const std::string& toDest, const Key& servingSealingKey,
        const std::string& deliveryClass, const std::string& mailbox,
        const std::optional<Bytes>& token, const Bytes& payload,
        const std::function<void()>& onAcceptedByOwnServer = {}, bool* tokenRejected = nullptr,
        std::string* outAttemptId = nullptr, bool waitForOutcome = true);
    void persistContacts() const;
    void persistMeta() const;
    // Serializes the in-memory contacts into the on-disk JSON shape.
    nlohmann::json contactsToJson() const;

    // --- Group helpers ---

    // Our own serving sealing key (SPKI DER, base64) - what contacts and group
    // members seal delivery envelopes to us with.
    std::string ownServingKeyB64() const;
    // Builds and hybrid-signs the current roster of a group (we must be an
    // admin) and returns it base64-encoded.
    std::string signedRosterB64(const std::string& groupId) const;
    // Verifies a signed roster (admin signature, group id) and applies it
    // (members/routing/admins/epoch), preserving any pool tokens we already hold.
    // Sets *membershipShrank when a member disappeared (a removal, so the caller
    // rotates its pool to cut off the removed member).
    void applyRoster(
        const std::string& groupId, const Bytes& rosterDer, bool* membershipShrank = nullptr);
    // Registers a fresh token pool for a group (recording its hashes so it can
    // be revoked later) and returns the raw tokens.
    std::vector<std::string> issueGroupPool(Group& group);
    // Revokes the pool we last issued to a group (deletes its hashes at our
    // server), so its tokens stop working.
    void revokeGroupPool(Group& group);
    // Issues our token pool for a group and sends it (group.tokens, tokenless
    // contact class) to every other member.
    void broadcastGroupPool(const std::string& groupId);
    // Revokes our old pool and broadcasts a fresh one - used after a removal so a
    // removed member's stash of our tokens is invalidated.
    void rotateGroupPool(const std::string& groupId);
    // Re-signs the current roster and broadcasts it (group.roster) to all members.
    void broadcastRoster(const std::string& groupId);
    // Delivers a built inner payload to one group member over the contact class
    // (tokenless): used for pool distribution.
    void sendToMemberContact(const std::string& memberFp, const GroupMember& member,
        const nlohmann::json& inner);
    // Delivers a built inner payload to one group member over the content class,
    // spending one of that member's pool tokens.
    void sendToMemberContent(
        const std::string& groupId, const std::string& memberFp, const nlohmann::json& inner);
    void persistGroups() const;
    nlohmann::json groupsToJson() const;

    std::filesystem::path profileDir_;
    // Fetches an externalized blob: direct over a transient I2P destination, falling
    // back to the own-server I2P proxy when this client has no I2P transport of its own.
    Bytes fetchLargeBlob(const BlobPointer& pointer);
    // Same, but streams the blob straight to dest so a large attachment never
    // sits whole in memory. The direct path is streamed; the proxy fallback
    // (clients with no I2P transport) still buffers the ciphertext through the facade.
    void fetchLargeBlobToFile(const BlobPointer& pointer, const std::filesystem::path& dest,
        const UploadProgressFn& onProgress = {}, const BlobStageFn& onStage = {},
        const std::atomic<bool>* cancel = nullptr);
    // Deletes an externalized blob (unsend): direct over a transient I2P destination,
    // falling back to the own-server I2P proxy.
    void deleteLargeBlob(const std::string& blobUrl, const std::string& deleteToken);
    // Records / persists the blob externalized for a sent message, so it can be
    // unsent later.
    void recordSentBlob(
        const std::string& messageId, const std::string& blobUrl, const std::string& deleteToken);
    void loadSentBlobs();
    void persistSentBlobs() const;

    std::unique_ptr<Client> client_;
    // The central alias resolver this profile resolves usernames against.
    ResolverCoordinate resolverCoordinate_ = defaultResolverCoordinate();
    bazarish::i2p::Privacy blobFetchPrivacy_ = bazarish::i2p::Privacy::eMax;
    // The embedded I2P router is process-global (the i2pd engine allows only one
    // per process), so every profile shares the one instance (see sharedI2pRouter).
    // It is started lazily on first transport use, so offline operations and tests
    // that never reach the network pay nothing.
    bazarish::i2p::Router& i2pRouter() const;

    // Injected audio/video device backends (empty -> the built-in synthetic
    // backend).
    AudioSourceFactory audioSourceFactory_;
    AudioSinkFactory audioSinkFactory_;
    VideoSourceFactory videoSourceFactory_;
    VideoSinkFactory videoSinkFactory_;

    // The single in-flight call. Media objects are non-null only while active.
    struct ActiveCall {
        CallState state = CallState::eIdle;
        std::string callId;
        std::string peerFingerprint;
        std::string peerMediaDest;  // the peer's media datagram routing address
        Bytes mediaKey;             // 32-byte AES-256-GCM key, shared both ways
        bool initiator = false;     // true on the caller side (nonce role prefix)
        bool video = false;         // true for a video call (audio plus video)
        bool muted = false;
        bool cameraOff = false;     // local camera disabled on a video call
        std::shared_ptr<bazarish::i2p::Endpoint> dgram;
        std::unique_ptr<I2pCallTransport> transport;
        std::unique_ptr<CallMedia> media;
    };
    ActiveCall call_;
    std::map<std::string, SentBlob> sentBlobs_;
    Key sealingKey_;
    std::map<std::string, Contact> contacts_;
    // Groups this client belongs to, by group id (client-side only).
    std::map<std::string, Group> groups_;
    // Our own serving destination + serving sealing key (SPKI DER, base64),
    // learned on subscribe (GET /v1/messaging/destination) and forwarded to
    // contacts in the E2E bootstrap so they route and seal replies to us.
    std::string myDest_;
    std::string myServingKeyB64_;
    // Human label for the profile picker (stored in the clear in meta.json).
    std::string name_;
    // The user's own avatar (compressed PNG/JPEG bytes) and its mime. The bytes
    // live in a sealed profile file; the mime is recorded in meta.json. Empty
    // when no avatar is set.
    Bytes avatar_;
    std::string avatarMime_;
    // Our own subscription certificate (DER, base64), retained on subscribe
    // so we can publish the full self-verifying chain in an invite.
    std::string subscriptionCertB64_;
    // Whether the private key PEMs are encrypted at rest. Persisted in meta so
    // open() knows to require a passphrase.
    bool encrypted_ = false;
    // The at-rest passphrase, retained for the session lifetime so contacts
    // (delivery tokens) can be re-sealed on every change. Empty when the
    // profile is unencrypted.
    std::string passphrase_;
    // User-owned I2P destination (per-user / portable address path). Empty when
    // the profile uses the server's address pool instead. The master private
    // key (i2p-master.dat) is the user's long-term routing identity; the active
    // transient (i2p-transient.dat) is the time-boxed delegation for the
    // current serving server. Both are sealed at rest when the profile is
    // encrypted.
    Bytes i2pMaster_;
    std::string i2pAddress_;
    Bytes i2pTransient_;

    // Writes an I2P key blob, sealed under the passphrase when encrypted.
    void persistI2pBlob(const std::string& filename, const Bytes& blob) const;
};

// The sign-in-with-key wire contract, shared with the service node's portal
// verifier: the challenge is signed as the body of a canonical request under
// this fixed method/path (so a login blob can never be replayed as a real API
// call), and the four hybrid-auth headers are packed into base64(JSON).
inline constexpr const char* kLoginMethod = "BZ-LOGIN";
inline constexpr const char* kLoginPath = "/portal/login";

// Verifies a login blob against the challenge it was issued for (freshness
// window enforced by the underlying request auth) and returns the signer's
// fingerprint. Throws on any failure. The verifier of record is the service
// node; this mirror lets the client round-trip and test its own blobs.
std::string verifyLoginBlob(
    const std::string& blob, std::int64_t now, const std::string& challenge);

}  // namespace bazarish::client
