// Bazarish project (c) 2026
#pragma once

#include "Client.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Crypto.hpp>

#include <cstdint>
#include <filesystem>
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
    // "contact-request" or "message".
    std::string kind;
    std::string fromFingerprint;
    std::string text;
    // True when this item established or refreshed a contact (carried the
    // peer's sealing key and a fresh token batch).
    bool establishedContact = false;
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
    // Creates a fresh identity and sealing key under stateDir.
    static Session create(const std::filesystem::path& stateDir, const ServerEndpoint& endpoint);
    // Opens an existing session.
    static Session open(const std::filesystem::path& stateDir);

    std::string fingerprint() const;
    std::string sealingPublicB64() const;

    // Subscribes to the configured server for the given number of days and
    // registers this client ID. Stores the returned server card.
    void subscribe(std::int64_t days);

    // Sends a contact request to a peer. The peer's prekey, serving server
    // and server card are looked up (on peerHost:peerPort when given — the
    // peer's facade for a cross-server contact — otherwise on our own); the
    // request payload is E2E-encrypted to the peer's prekey and carries a
    // fresh token batch and our own server card so the peer can reply.
    void sendContactRequest(const std::string& peerFingerprint, const std::string& text,
        const std::string& peerHost = {}, int peerPort = 0,
        const std::string& peerBasePath = {});

    // Sends an E2E-encrypted message to an established contact, spending one
    // of the peer's tokens. Throws if the contact is unknown or out of
    // tokens. When the contact has no reciprocal tokens from us yet (the
    // first reply), a fresh batch for the peer is registered and attached.
    void sendMessage(const std::string& peerFingerprint, const std::string& text);

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

    // Seals a delivery envelope to the destination server's sealing key and
    // submits it, polling to completion.
    void deliver(const std::string& toServer, const Key& serverSealingKey,
        const std::string& kind, const std::string& mailbox,
        const std::optional<Bytes>& token, const Bytes& payload);
    void persistContacts() const;
    void persistMeta() const;

    std::filesystem::path stateDir_;
    std::unique_ptr<Client> client_;
    Key sealingKey_;
    std::map<std::string, Contact> contacts_;
    // Our own server card (DER, base64), learned on subscribe and forwarded
    // to contacts inside the E2E payload so they can seal replies to our
    // server.
    std::string serverCardB64_;
};

}  // namespace bazarish::client
