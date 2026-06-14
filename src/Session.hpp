// Bazarish project (c) 2026
#pragma once

#include "Client.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Crypto.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bazarish::client {

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
    // For contentType == "receipt", the message id being acknowledged.
    std::string refId;

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

    // Registers a human-readable alias (username) for this identity at the
    // serving server's service node, so contacts can add us by name.
    void registerAlias(const std::string& alias);

    // Sends a contact request to a peer. The peer's prekey, serving server
    // and server card are looked up (on peerHost:peerPort when given — the
    // peer's facade for a cross-server contact — otherwise on our own); the
    // request payload is E2E-encrypted to the peer's prekey and carries a
    // fresh token batch and our own server card so the peer can reply.
    void sendContactRequest(const std::string& peerFingerprint, const std::string& text,
        const std::string& peerHost = {}, int peerPort = 0,
        const std::string& peerBasePath = {});

    // A bazarish:// invite carrying our full self-verifying serving chain
    // (subscription certificate + server card). A contact can verify it and
    // reach us with no trust in any server. Requires an active subscription.
    std::string inviteUri() const;

    // Adds a contact from an invite URI: the embedded chain is verified
    // offline (no lookup) and a contact request is sent to the peer.
    void addByInvite(const std::string& inviteUri, const std::string& text);

    // Adds a contact by username (alias). The resolver maps the alias to a
    // fingerprint, which is the one trust compromise — a hostile resolver
    // could return a wrong fingerprint; everything after the mapping is
    // verified end-to-end. host/port target the resolver's facade (our own
    // when host is empty).
    void addByUsername(const std::string& alias, const std::string& text,
        const std::string& host = {}, int port = 0, const std::string& basePath = {});

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

    // Sends a delivery receipt (content type "receipt") acknowledging that we
    // received the message with id refMessageId. Costs one delivery token.
    void sendReceipt(const std::string& peerFingerprint, const std::string& refMessageId);

    // Downloads a content-store attachment (from a received message) and
    // decrypts it to dest.
    void saveAttachment(const std::string& ref, const std::string& keyB64,
        const std::filesystem::path& dest);

    // Pulls, decrypts, applies (contacts/tokens) and acks all pending items.
    std::vector<IncomingMessage> sync();

    bool hasContact(const std::string& peerFingerprint) const;
    // Fingerprints of all known contacts, for UI listing.
    std::vector<std::string> contactFingerprints() const;

private:
    Session(std::filesystem::path stateDir, std::unique_ptr<Client> client, Key sealingKey,
        std::map<std::string, Contact> contacts);

    // Generates a token batch for ourselves: registers the hashes with our
    // server and returns the raw tokens (base64) to hand to the peer.
    std::vector<std::string> issueTokenBatch();

    // Looks up a contact on a given facade (defaults to our own when host is
    // empty), returning the verified prekey certificate and server card.
    ContactInfo lookupContactAt(
        const std::string& host, int port, const std::string& basePath,
        const std::string& peerFingerprint) const;

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
    void deliver(const std::string& toServer, const Key& serverSealingKey,
        const std::string& deliveryClass, const std::string& mailbox,
        const std::optional<Bytes>& token, const Bytes& payload,
        const std::function<void()>& onAcceptedByOwnServer = {});
    void persistContacts() const;
    void persistMeta() const;
    // Serializes the in-memory contacts into the on-disk JSON shape.
    nlohmann::json contactsToJson() const;

    std::filesystem::path stateDir_;
    std::unique_ptr<Client> client_;
    Key sealingKey_;
    std::map<std::string, Contact> contacts_;
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
};

}  // namespace bazarish::client
