// Bazarish project (c) 2026
#pragma once

#include "Client.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Sam.hpp>

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
    // Fingerprint of the server currently serving the peer.
    std::string server;
    // The peer's serving server's sealing public key (SPKI DER, base64):
    // delivery envelopes to this contact are sealed to it.
    std::string serverSealingB64;
    // Unused one-time delivery tokens (base64) issued by the peer to us:
    // each authorizes one message into the peer's mailbox.
    std::vector<std::string> sendTokens;
    // Whether we have already issued a token batch to this peer (so they
    // can write to us). Set on the contact request or the first reply.
    bool issuedToThem = false;
};

// A member of a group: routing plus the one-time token pool that member issued
// to the group (we spend these to deliver to them). Mirrors the reachable half
// of a Contact, but scoped to a single group.
struct GroupMember {
    std::string sealingPublicB64;
    std::string server;
    std::string serverSealingB64;
    std::vector<std::string> sendTokens;
    bool admin = false;
};

// A group as this client knows it: the roster (other members + their routing and
// pools), the admin set (implied by member admin flags), and the roster epoch.
// Purely client-side — the server never sees a group (see docs Groups.md).
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
    // server and a fresh token batch) — a new or refreshed contact.
    bool establishedContact = false;
    // The original type string when contentType == "unsupported".
    std::string rawType;
    // The sender's protocol message id (envelope "id"), used to send a
    // delivery receipt back for it.
    std::string messageId;
    // For contentType == "receipt", the message id being acknowledged; for
    // contentType == "bot.callback", the keyboard message the button belongs to.
    std::string refId;

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
};

// The stateful client session: a user identity plus contact and token
// bookkeeping persisted under a state directory, layered over the stateless
// Client API wrappers. This is the logic a GUI or CLI front-end drives.
//
// Token model (per Contacts.md): to let a peer write to us we generate a
// batch, register its hashes with our own server and hand the raw tokens to
// the peer; to write to a peer we spend a token the peer gave us. Contact
// bootstrap exchanges both directions over a tokenless contact request and
// its reply.
class Session {
public:
    // Creates a fresh identity and sealing key under stateDir, with no server
    // connection yet. A non-empty passphrase encrypts the private key PEMs at
    // rest (AES-256-CBC); name is a human label stored in the clear for the
    // profile picker. Use connectServer() + subscribe() to attach a server.
    static Session create(const std::filesystem::path& stateDir,
        const std::string& passphrase = {}, const std::string& name = {});
    // Creates a profile already bound to a server (convenience for the CLI and
    // tests): equivalent to create() followed by connectServer().
    static Session create(const std::filesystem::path& stateDir, const ServerEndpoint& endpoint,
        const std::string& passphrase);
    // Opens an existing session. The passphrase is required when the keys
    // were created encrypted; it is ignored for unencrypted keys.
    static Session open(const std::filesystem::path& stateDir, const std::string& passphrase = {});

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
    // holds the keys in plain PEM internally — the password protects the file.
    void exportState(
        const std::filesystem::path& outFile, const std::string& password) const;
    // Imports an exported bundle into a fresh stateDir. A non-empty
    // atRestPassphrase re-encrypts the imported keys on disk.
    static void importState(const std::filesystem::path& bundleFile,
        const std::filesystem::path& stateDir, const std::string& password,
        const std::string& atRestPassphrase = {});

    std::string fingerprint() const;
    std::string sealingPublicB64() const;
    // The human label set at creation (may be empty).
    const std::string& displayName() const;

    // Subscribes to the configured server for the given number of days and
    // registers this client ID. Stores the returned server card.
    void subscribe(std::int64_t days);

    // User-owned I2P destination (the per-user / "paid" path). Free profiles
    // route through the server's address pool and never call these.
    // ensureI2pDestination mints the permanent ("master") key the first time
    // and persists it sealed at rest, returning the stable base32 address;
    // subsequent calls are idempotent. The master never leaves the client.
    std::string ensureI2pDestination();
    bool hasI2pDestination() const;
    // The stable base32 address (without the ".b32.i2p" suffix), or empty.
    std::string i2pAddress() const;
    // Issues a fresh time-boxed transient (offline keys) from the master, valid
    // until expiresUnix — the delegation handed to the serving server to operate
    // the destination for the subscription window. Throws if the profile has no
    // user-owned destination. subscribe() calls this automatically when one
    // exists, for the subscription period.
    void renewI2pTransient(std::int64_t expiresUnix);
    // The active transient blob to hand to the serving server (empty if none).
    Bytes i2pTransient() const;

    // Sign-in-with-key: signs an opaque challenge issued by a service portal,
    // proving ownership of this identity's key without the key ever reaching
    // the browser. Returns a base64 token the user pastes back into the portal,
    // which verifies it (see verifyLoginBlob) and recovers this fingerprint.
    std::string signLogin(const std::string& challenge) const;

    // Registers a human-readable alias (username) for this identity at the
    // serving server's service node, so contacts can add us by name.
    void registerAlias(const std::string& alias);

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

    // Adds a contact from an invite URI: the embedded chain is verified
    // offline (no lookup) and a contact request is sent to the peer.
    void addByInvite(const std::string& inviteUri, const std::string& text);

    // Adds a contact by username (alias) registered on our own server. The
    // resolver maps the alias to a fingerprint, which is the one trust
    // compromise — a hostile resolver could return a wrong fingerprint;
    // everything after the mapping is verified end-to-end. The resolver is our
    // own server's service node (facade locality — we never query a foreign
    // facade); a cross-server @alias awaits the sealed federated resolve.
    void addByUsername(const std::string& alias, const std::string& text);

    // Sends an E2E-encrypted message to an established contact, spending one
    // of the peer's tokens. Throws if the contact is unknown or out of
    // tokens. When the contact has no reciprocal tokens from us yet (the
    // first reply), a fresh batch for the peer is registered and attached.
    // messageId, when given, is used as the protocol message id (so a delivery
    // receipt can be matched back). onAcceptedByOwnServer fires once when our
    // own server has accepted the envelope into its buffer (the "grey" state),
    // before the recipient server confirms storage (the "yellow" state, which
    // is this call returning normally).
    void sendMessage(const std::string& peerFingerprint, const std::string& text,
        const std::string& messageId = {},
        const std::function<void()>& onAcceptedByOwnServer = {});

    // Sends a file as a "file" content message: the bytes are encrypted with a
    // fresh key and uploaded to the content store; the message carries the
    // reference and key end-to-end. The server never sees the content type.
    void sendFile(const std::string& peerFingerprint, const std::filesystem::path& path,
        const std::string& messageId = {},
        const std::function<void()>& onAcceptedByOwnServer = {});

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
    void sendEdit(const std::string& peerFingerprint, const std::string& refMessageId,
        const std::string& text, const InlineKeyboard& keyboard = {});

    // Sends a delivery receipt (content type "receipt") acknowledging that we
    // received the message with id refMessageId. Costs one delivery token.
    void sendReceipt(const std::string& peerFingerprint, const std::string& refMessageId);

    // Downloads a content-store attachment (from a received message) and
    // decrypts it to dest.
    void saveAttachment(const std::string& ref, const std::string& keyB64,
        const std::filesystem::path& dest);

    // Selects the I2P tunnel privacy profile used when fetching externalized
    // large blobs over a transient SAM session. Defaults to the most private.
    void setBlobFetchPrivacy(I2pPrivacy privacy);

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

private:
    Session(std::filesystem::path stateDir, std::unique_ptr<Client> client, Key sealingKey,
        std::map<std::string, Contact> contacts);

    // Generates a token batch for ourselves: registers the hashes with our
    // server and returns the raw tokens (base64) to hand to the peer.
    std::vector<std::string> issueTokenBatch();

    // Sends a contact request to a peer whose verified routing info is
    // already known (from a lookup or an invite). Mints a reply token batch
    // and records the contact.
    void requestWithInfo(const std::string& peerFingerprint, const std::string& text,
        const ContactInfo& info);

    // Sends a built inner content envelope to an established contact: handles
    // the first-reply bootstrap, seals to the peer and spends one token.
    void sendContent(const std::string& peerFingerprint, nlohmann::json inner,
        const std::function<void()>& onAcceptedByOwnServer = {});

    // Mints a fresh token batch for the peer and sends it as a token-refill,
    // in response to the peer signalling a low stash (Contacts.md).
    void sendTokenRefill(const std::string& peerFingerprint);

    // Seals a delivery envelope to the destination server's sealing key and
    // submits it, polling to completion. onAcceptedByOwnServer fires once when
    // our own server first accepts the envelope (the "grey" delivery state).
    // When tokenRejected is non-null, a token-rejected failure (the token was
    // already spent — e.g. a concurrent group sender took it) does not throw;
    // it sets *tokenRejected and returns, so the caller can retry with another
    // token (group fan-out's optimistic retry).
    void deliver(const std::string& toServer, const Key& serverSealingKey,
        const std::string& deliveryClass, const std::string& mailbox,
        const std::optional<Bytes>& token, const Bytes& payload,
        const std::function<void()>& onAcceptedByOwnServer = {}, bool* tokenRejected = nullptr);
    void persistContacts() const;
    void persistMeta() const;
    // Serializes the in-memory contacts into the on-disk JSON shape.
    nlohmann::json contactsToJson() const;

    // --- Group helpers ---

    // Our own server's sealing key (SPKI DER, base64), derived from our server
    // card — what other members seal delivery envelopes to us with.
    std::string ownServerSealingB64() const;
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
    // Revokes our old pool and broadcasts a fresh one — used after a removal so a
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

    std::filesystem::path stateDir_;
    std::unique_ptr<Client> client_;
    I2pPrivacy blobFetchPrivacy_ = I2pPrivacy::kMax;
    Key sealingKey_;
    std::map<std::string, Contact> contacts_;
    // Groups this client belongs to, by group id (client-side only).
    std::map<std::string, Group> groups_;
    // Our own server card (DER, base64), learned on subscribe and forwarded
    // to contacts inside the E2E payload so they can seal replies to our
    // server.
    std::string serverCardB64_;
    // Human label for the profile picker (stored in the clear in meta.json).
    std::string name_;
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
