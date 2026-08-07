// Bazarish project (c) 2026
#pragma once

#include "AudioIo.hpp"
#include "CallMedia.hpp"
#include "Client.hpp"
#include "FileTransfer.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/I2p.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
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
    // The server-side pending-blob id this item was fetched as. NOT acked during
    // sync(): the surfaced item is acked only after the client has durably stored
    // it (ackPending, driven by the GUI after persistence), so a crash/restart
    // between fetch and store never loses it. Empty for items the core consumed
    // itself (acked in sync()).
    std::string pendingId;
    // The sender's send time (envelope "sentAt", unix milliseconds). The
    // recipient orders by it and shows it as the message time, not the receive
    // time (docs-main Messages.md "Ordering and timestamps"). 0 when absent.
    std::int64_t sentAt = 0;
    // For contentType == "receipt", the message id being acknowledged; for
    // contentType == "bot.callback", the keyboard message the button belongs to.
    std::string refId;

    // When this message replies to another, the protocol id of that original
    // message (so the UI can render a quote and link to it). Empty when not a reply.
    std::string replyTo;

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
// A file this client announced in a message it sent. The bytes were never
// uploaded anywhere, so serving a later request means reading this path again -
// which is also why the sender can "unsend" simply by forgetting it.
struct SentFile {
    std::filesystem::path path;
    std::string sha256;  // plaintext digest, as announced in the message
    std::uint64_t size = 0;
};

// What a direct transfer is doing, so the message block can show it honestly
// instead of pretending the file is already delivered.
enum class TransferState {
    eRequested,  // we asked; waiting for the sender to come up
    eRunning,
    eDone,
    eFailed,
};

struct TransferEvent {
    std::string messageId;
    TransferState state = TransferState::eRequested;
    std::uint64_t bytes = 0;
    std::uint64_t total = 0;
    std::string error;  // set when state == eFailed
};

using TransferEventFn = std::function<void(const TransferEvent&)>;

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
    // Changes the account's own display name. Local only: it rewrites the profile
    // meta and so the name carried in future invite descriptors (inviteUri), but it
    // is NEVER sent to contacts - each contact controls the name they keep for us.
    void setDisplayName(const std::string& name);

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
    // Whether this contact sent us a request we have not yet accepted (we hold
    // their tokens but have not issued ours). Drives the "Agree" affordance.
    bool contactIsPending(const std::string& peerFingerprint) const;
    // Renames a contact locally and mirrors the change to the account's other
    // devices (a device.contact-name self-message). No-op for an unknown contact.
    void renameContact(const std::string& peerFingerprint, const std::string& name);

    // Mirrors a chat pin/unpin to the account's other devices (a device.chat-pin
    // self-message). The pin list itself lives in the GUI's local store; this only
    // broadcasts the change so every device keeps the same pinned chats.
    void syncChatPinToSelf(const std::string& peerFingerprint, bool pinned);

    // Permanently removes a contact: drops it from the contact list, deletes its sealed
    // avatar blob, and persists. Local only and irreversible - the peer is not
    // told. No-op for an unknown contact. The caller wipes the local transcript.
    void removeContact(const std::string& peerFingerprint);

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
    // The user's own storage usage on the mailbox + blob backends (used/quota each),
    // for the per-profile settings view. Best effort - never throws.
    StorageUsage storageUsage();
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

    // Accepts a received contact request: sends a "contact.accept" back, which (as
    // our first reply) carries our descriptor and a reply-token batch, so the
    // requester becomes a fully mutual contact and a one-to-one chat opens. A no-op
    // if we cannot reach the requester (no contact / tokens) yet.
    void acceptContactRequest(const std::string& peerFingerprint);

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

    // --- Asynchronous contact add ------------------------------------------------
    // addByInvite / addByUsername above are synchronous: they block on a federated
    // card fetch (the serving server dials the peer over I2P, tens of seconds when
    // the peer is slow or unreachable). A GUI must never run that on the thread that
    // also drives sync and the connection, or the whole profile freezes until the
    // fetch returns. The three steps below split it so the slow fetch runs off the
    // worker thread on its own transport, and only the fast finalize touches the
    // session - keeping the connection live throughout.

    // What resolveContactCard needs, snapshotted from the live session so the
    // (background) resolve depends on nothing the session may mutate or free.
    struct ContactFetchContext {
        std::string identityPem;        // unencrypted in-memory private PEM
        std::string clientId;
        ServerEndpoint endpoint;
        std::filesystem::path i2pDataDir;
        ResolverCoordinate resolver;
        bool i2pEnabled = false;
        bazarish::i2p::Privacy blobFetchPrivacy = bazarish::i2p::Privacy::eMax;
    };
    // An add to resolve: an invite URI (byUsername=false) or an alias.
    struct ContactCardRequest {
        bool byUsername = false;
        std::string uriOrAlias;
        std::string introText;
    };
    // The outcome of an off-thread resolve, finalized by commitContactAdd. On
    // failure ok is false and error carries a human-readable reason (no throw).
    struct ContactCardResolved {
        bool ok = false;
        std::string error;
        std::string fingerprint;
        ContactInfo info;
        std::string displayName;
        std::string introText;
    };

    // [worker thread] Snapshot the transport context for an off-thread resolve.
    ContactFetchContext contactFetchContext() const;
    // [any thread] Resolve and verify a contact card using a PRIVATE throwaway
    // transport (its own connection, so it never contends with the session's sync
    // transport). Touches no session state; never throws.
    static ContactCardResolved resolveContactCard(
        const ContactFetchContext& context, const ContactCardRequest& request);
    // [worker thread] Finalize a resolved add: send the contact request and record
    // the contact. Returns the contact fingerprint. Throws on a delivery failure.
    std::string commitContactAdd(const ContactCardResolved& resolved);

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
    // replyTo, when set, is the protocol id of the message this one replies to;
    // it rides in the envelope so the recipient can render a quote and link to
    // the original (a no-op reference if they do not hold it locally).
    bool sendMessage(const std::string& peerFingerprint, const std::string& text,
        const std::string& messageId = {},
        const std::function<void()>& onAcceptedByOwnServer = {},
        std::string* outAttemptId = nullptr, const std::string& replyTo = {});

    // Announces a file as a "file" content message: name, size and digest only.
    // The bytes never leave this machine until the recipient asks for them, so
    // the send itself is instant regardless of file size - and the file must
    // still be at this path, and this client online, when they do.
    bool sendFile(const std::string& peerFingerprint, const std::filesystem::path& path,
        const std::string& messageId = {},
        const std::function<void()>& onAcceptedByOwnServer = {},
        std::string* outAttemptId = nullptr, const std::string& replyTo = {});

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

    // Sets our reaction (an emoji) to a one-to-one message: content type "reaction"
    // referencing refMessageId. An empty emoji removes our reaction. One reaction per
    // user per message - a new one overwrites the old at the recipient.
    void sendReaction(const std::string& peerFingerprint, const std::string& refMessageId,
        const std::string& emoji);

    // Asks the peer to clear the whole conversation with us (content type
    // "chat.clear"): on receipt their client wipes its transcript with us, the
    // same way it auto-applies a delete-for-everyone. Costs one delivery token.
    void sendChatClear(const std::string& peerFingerprint);

    // The outcome of a still-in-flight send, re-polled after the initial submit
    // window. status is "pending", "delivered", "failed", or "unknown" (the
    // server no longer knows the attempt - it expired or the server restarted).
    struct AttemptOutcome {
        std::string status;
        std::string errorMessage;
        // The server's live federation phase while still pending (empty otherwise).
        std::string phase;
    };
    // Re-polls a previously submitted send by its server attempt id to resolve a
    // delivery that was still pending when the send call returned. Never throws.
    AttemptOutcome pollAttempt(const std::string& attemptId);

    // Asks the sender of an announced file to serve it, and downloads it to dest
    // when the sealed offer comes back. Returns at once: the transfer runs in the
    // background and reports through the transfer handler, because it depends on
    // the other side being online and can take as long as I2P takes.
    void requestFile(const std::string& peerFingerprint, const std::string& messageId,
        const std::filesystem::path& dest);

    // Abandons a running or requested transfer.
    void cancelTransfer(const std::string& messageId);

    // Where transfer progress and outcomes are reported. One handler for the
    // whole session; events carry the message id they belong to.
    void setTransferHandler(TransferEventFn handler);

    // Sender unsend: forgets the file announced for a message we sent, so a later
    // request from the recipient is answered "no longer available". Nothing has
    // to be deleted anywhere else - the bytes were never copied off this machine.
    void unsend(const std::string& messageId);

    // Selects the I2P tunnel privacy profile used for direct transfers (both
    // serving and fetching, always over one-time destinations). Defaults to the
    // most private.
    void setTransferPrivacy(bazarish::i2p::Privacy privacy);

    // --- Audio calls (client-to-client; signalling over E2E, media over I2P) ---

    // Lifecycle of the single call this session tracks at a time.
    enum class CallState {
        eIdle,
        eOutgoing,  // we invited; awaiting the peer's accept
        eIncoming,  // a call.invite arrived; awaiting our accept/decline
        eActive,    // media is flowing
    };

    // How a finished call ended, for the chat-history record.
    enum class CallOutcome {
        eAnswered,   // connected and hung up normally (durationSec is meaningful)
        eNoAnswer,   // outgoing: the peer never answered before the ring timeout
        eDeclined,   // explicitly rejected (by the peer for our outgoing call, by us
                     // for an incoming one)
        eMissed,     // incoming: we never answered (timeout) or the caller cancelled
        eCancelled,  // outgoing: we hung up before the peer answered
        eBusy,       // outgoing: the peer was already in another call
    };

    // A finished call awaiting a chat-history entry. Drained by takeCallLog().
    struct CompletedCall {
        std::string peer;
        bool incoming = false;
        CallOutcome outcome = CallOutcome::eMissed;
        std::int64_t durationSec = 0;  // connected duration; 0 unless eAnswered
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

    // Drains the calls that finished since the last call - each needs a chat-history
    // entry (incoming/outgoing + how it ended). Empty when nothing finished.
    std::vector<CompletedCall> takeCallLog();
    // Advances the call's ring/answer timeout so a call never rings forever: an
    // unanswered outgoing or incoming call is torn down and queued for takeCallLog()
    // (an outgoing timeout also cancels the peer). Call periodically - the GUI does
    // so on every sync tick; a no-op when there is no ringing call.
    void tickCalls();

    // Pulls and decrypts pending items, applies their contact/token side effects,
    // and acks items the core consumes itself. autoAckSurfaced (default true) acks a
    // SURFACED item in the loop too - the simple behaviour the CLI and bots want.
    // The GUI passes false so a surfaced item is NOT acked here (it comes back with a
    // non-empty pendingId); the GUI acks it via ackPending only after durably
    // persisting it, so a crash between fetch and store never loses a message.
    std::vector<IncomingMessage> sync(bool autoAckSurfaced = true);

    // Acks a pending mailbox item by its server-side blob id (IncomingMessage's
    // pendingId), removing it from the mailbox. Called after the item has been
    // durably persisted on the client side. Throws if the server is unreachable.
    void ackPending(const std::string& pendingId);

    bool hasContact(const std::string& peerFingerprint) const;
    // Fingerprints of all known contacts, for UI listing.
    std::vector<std::string> contactFingerprints() const;


private:
    Session(std::filesystem::path profileDir, std::unique_ptr<Client> client, Key sealingKey,
        std::map<std::string, Contact> contacts);

    // Generates a token batch for ourselves: registers the hashes with our
    // server and returns the raw tokens (base64) to hand to the peer.
    std::vector<std::string> issueTokenBatch();
    // Mints ONE fresh delivery token (registers its hash with our server) and
    // returns it (base64). Used to prepay a specific reply - a low-stash request
    // embeds one so the peer's token-refill reply is always deliverable.
    std::string issueOneToken();

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
    // establishOnFirstReply (default true): on the FIRST content we send to a peer
    // that wrote to us first, attach our bootstrap (routing + a reply-token batch)
    // and mark the contact accepted (issuedToThem). A read receipt passes false so
    // it can confirm a read WITHOUT auto-accepting an un-accepted contact request -
    // the request is still accepted explicitly (Agree) or by sending a real message.
    // overrideToken, when non-empty, is spent to deliver this message INSTEAD of one
    // from our own stash of the peer's tokens (and our stash is left untouched): used
    // for a token-refill reply, which the peer's request prepaid with a fresh token,
    // so the reply is deliverable even when we hold none of their tokens.
    bool sendContent(const std::string& peerFingerprint, nlohmann::json inner,
        const std::function<void()>& onAcceptedByOwnServer = {},
        std::string* outAttemptId = nullptr, bool waitForOutcome = false,
        bool establishOnFirstReply = true, const std::string& overrideToken = {});

    // Sends the user-owned I2P master to the account's other devices: a
    // service content message ("device.i2p-master") sealed to our own sealing
    // key and delivered tokenlessly to our own destination, so it lands in our
    // own mailbox and every device of this account picks it up on sync and
    // persists the same master (preserving the b32 across devices). Best effort;
    // a no-op when there is no master or our routing is not known yet.
    void syncI2pMasterToSelf();

    // Mints a fresh token batch for the peer and sends it as a token-refill, in
    // response to the peer signalling a low stash (Contacts.md). prepaidToken, when
    // non-empty, is a fresh token the peer embedded in its low-stash request: the
    // refill is delivered by spending exactly it, so the reply always lands even when
    // we hold none of the peer's own tokens (the refill round-trip funds itself).
    void sendTokenRefill(const std::string& peerFingerprint, const std::string& prepaidToken = {});

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
    // Queues a chat-history entry for the current call with the given outcome (using
    // its peer, direction and connected time). Call before clearCall().
    void logCompletedCall(CallOutcome outcome);

    // Seals a delivery envelope to the destination server's sealing key and
    // hands it to our own server, which accepts it at once (store-and-forward)
    // and federates in the background. onAcceptedByOwnServer fires once on that
    // acceptance (the "grey" delivery state). Returns true if the server
    // confirmed the recipient server stored it within the poll window (the
    // "yellow" state), false if it was accepted but is still being delivered
    // (stays grey - the server keeps retrying and a read receipt confirms it
    // later). Throws on a terminal failure. waitForOutcome=false
    // skips the poll entirely: it submits, fires the grey callback and returns
    // false at once (used for large sends so the upload never adds a poll wait on
    // top; the caller reconciles the outcome on a later sync).
    bool deliver(const std::string& toDest, const Key& servingSealingKey,
        const std::string& deliveryClass, const std::string& mailbox,
        const std::optional<Bytes>& token, const Bytes& payload,
        const std::function<void()>& onAcceptedByOwnServer = {},
        std::string* outAttemptId = nullptr, bool waitForOutcome = true);
    void persistContacts() const;
    void persistMeta() const;
    // Serializes the in-memory contacts into the on-disk JSON shape.
    nlohmann::json contactsToJson() const;


    std::filesystem::path profileDir_;
    // A peer asked for a file we announced: encrypt it to a temp ciphertext, raise
    // a one-time destination, seal the offer back and serve until the window
    // closes. Runs on its own thread - building tunnels takes tens of seconds.
    void serveRequestedFile(const std::string& peerFingerprint, const std::string& fileId);
    // A sealed offer came back for a file we asked for: fetch it. Also threaded.
    void startAnnouncedFetch(const FileOffer& offer);
    void emitTransfer(const std::string& messageId, TransferState state, std::uint64_t bytes,
        std::uint64_t total, const std::string& error = {});
    void loadSentFiles();
    void persistSentFiles() const;

    std::unique_ptr<Client> client_;
    // The central alias resolver this profile resolves usernames against.
    ResolverCoordinate resolverCoordinate_ = defaultResolverCoordinate();
    bazarish::i2p::Privacy transferPrivacy_ = bazarish::i2p::Privacy::eMax;
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
        std::int64_t startedAtMs = 0;    // invite sent (outgoing) / received (incoming)
        std::int64_t connectedAtMs = 0;  // became active, for the call duration
        std::shared_ptr<bazarish::i2p::Endpoint> dgram;
        std::unique_ptr<I2pCallTransport> transport;
        std::unique_ptr<CallMedia> media;
    };
    ActiveCall call_;
    // Calls that finished but whose chat-history entry has not been drained yet.
    std::vector<CompletedCall> pendingCallLog_;
    std::map<std::string, SentFile> sentFiles_;
    // Transfers this client is driving, by message id: a pending entry is created
    // by requestFile and consumed when the offer arrives. Held behind a shared
    // pointer because the transfer threads outlive any particular Session object
    // (the session is movable, a mutex is not).
    struct PendingTransfer {
        std::filesystem::path dest;
        std::shared_ptr<std::atomic<bool>> cancel;
    };
    struct TransferRegistry {
        std::mutex mutex;
        std::map<std::string, PendingTransfer> pending;
        TransferEventFn onEvent;
    };
    std::shared_ptr<TransferRegistry> transfers_ = std::make_shared<TransferRegistry>();
    Key sealingKey_;
    std::map<std::string, Contact> contacts_;
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
