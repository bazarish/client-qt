// Bazarish project (c) 2026
#pragma once

#include "ApiClient.hpp"
#include "ResolverConfig.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Certificates.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Descriptor.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bazarish::client {

// This user's assigned serving destination + the public serving sealing key
// for it (from GET /v1/messaging/destination). Folded into the user-signed
// subscription certificate so contacts route and seal to it.
struct DestinationInfo {
    std::string dest;
    Bytes servingSealingKeyDer;
    // Destination state: "building" / "active" / "expired" (see server-core
    // PerUserDestinations), empty when the user delegated none.
    std::string state;
};

// The destination status the client polls (GET /v1/account/i2p-status): whether
// this node has an approved account for us, and the recorded validity of the
// delegation it holds (so any of the user's devices knows when to re-issue).
struct I2pDestStatus {
    // "none" (this node knows no such account), "pending" (a moderated node has
    // not approved it yet, so no destination is raised) or "approved".
    std::string approval;
    // What a moderated node tells a user still awaiting approval.
    std::string registrationMessage;
    // Validity of the delegation this account currently has with the server; 0
    // when it delegated none. The client re-issues before this lapses, or the
    // destination goes down.
    std::int64_t transientExpires = 0;
    std::int64_t transientUpdatedAt = 0;

    bool approved() const { return approval == "approved"; }
};

// The user's own storage usage on its two backends, polled for the per-profile
// settings view. Each half has an `ok` flag: a backend that did not answer (e.g.
// offline) leaves its figures at zero with ok=false, so the UI can show the part
// that succeeded and mark the rest stale.
struct StorageUsage {
    bool mailboxOk = false;
    std::uint64_t mailboxUsedBytes = 0;
    std::uint64_t mailboxQuotaBytes = 0;
};

// Server reply to publishing our card: what the account holds, plus the serving
// destination and serving sealing key the card now carries.
struct PublishResult {
    std::uint64_t quotaBytes = 0;
    // The card-read capability the node issued for this account: it goes in
    // every invite this profile makes.
    std::string view;
    // The user's assigned serving destination and its serving sealing key.
    std::string dest;
    Bytes servingSealingKeyDer;
    // The card we signed (DER), persisted so we can hand a contact the full
    // self-verifying card and recover our own routing after a restart.
    Bytes cardDer;
};

// A contact's card: the sealing prekey plus the routing (dest + serving sealing
// key), signed by them. Verified before being returned.
struct ContactInfo {
    ContactCard card;
};

struct PendingEntry {
    std::string id;
    // Delivery class: "content" or "contact" (the server-visible admission
    // selector, not the end-to-end content type).
    std::string deliveryClass;
};

// Onboarding discovery for a server (GET /v1/account/portal, unauthenticated):
// the operator's human message and the registration/portal link(s) a client
// shows when it cannot subscribe yet (e.g. the key is not registered). Carries
// no facade and no secret - just where to go to register.
struct PortalInfo {
    std::string message;
    std::vector<std::string> links;
};

// Outcome of a send attempt. status is "pending", "delivered" or "failed";
// on failure errorCode carries the typed reason when recognized.
struct SendStatus {
    std::string status;
    // The server's live federation phase while pending ("queued", "dialing",
    // "connected", "sending", "awaiting-ack"); empty when not reported / terminal.
    std::string phase;
    std::optional<ErrorCode> errorCode;
    std::string errorMessage;
};

// The opaque result of one federation fetch (card / alias resolve), as seen by
// the client: the served reply is `ok` with a `sealed` body, or `ok == false`
// with a typed errorCode (CARD_UNKNOWN / ALIAS_UNKNOWN). The transport is
// responsible only for moving the sealed bytes - never for reading them.
struct FetchOutcome {
    bool ok = false;
    Bytes sealed;
    std::string errorCode;
};

// Moves one sealed fetch frame ({op, sealed}) to a .b32.i2p destination and
// returns the sealed reply. Two implementations back this: a direct transient-I2P
// dial (preferred, our server uninvolved) and the own-server proxy relay
// (Client::relayFetch). The caller picks; the crypto stays in fetchCard/resolve.
using FetchTransport
    = std::function<FetchOutcome(const std::string& toDest, const std::string& op, const Bytes& sealed)>;

// Seals a delivery envelope to a destination server. deliveryClass is the
// server-visible admission selector ("content" or "contact"); mailbox is the
// recipient's fingerprint, messageId deduplicates retries, token is the
// one-time delivery token for "content" (absent for "contact"). The result is
// the opaque sealed blob the send endpoint expects.
Bytes sealDeliveryEnvelope(const std::string& deliveryClass, const std::string& mailbox,
    const std::string& messageId, const std::optional<Bytes>& token,
    const Key& recipientSealingKey);

// The client-side messenger: typed wrappers over the full client API,
// reusing the shared identity, certificate and crypto primitives. Owns the
// caller identity; non-copyable and non-movable (it hands out a reference to
// its identity internally).
class Client {
public:
    // i2pDataDir enables reaching facades whose host ends in ".b32.i2p" over the
    // embedded I2P transport; empty leaves only clearnet facades usable.
    Client(Identity identity, std::string clientId, ServerEndpoint endpoint,
        std::filesystem::path i2pDataDir = {});

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    const Identity& identity() const;
    const std::string& clientId() const;
    const ServerEndpoint& endpoint() const;
    // The facade the transport is currently using (last that worked), as a URL.
    std::string activeFacadeUrl() const;
    // Whether that facade is an I2P facade (for the account list marking).
    bool activeFacadeIsI2p() const;

    // Sticky I2P (see ApiClient): this profile refuses clearnet once it has
    // reached its server over I2P, until the user allows it again.
    void setI2pProven(bool proven);
    bool i2pProven() const;
    void setAllowClearnet(bool allow);
    bool allowClearnet() const;
    void setOnI2pProven(std::function<void()> callback);
    // Names this profile on the destinations this client creates (status view).
    void setDestinationOwner(std::string owner);
    // Drops the destination this client dials through; the next request builds a
    // fresh one.
    void releaseI2pLink();
    // The key a session secret is sealed to: this user's serving sealing key,
    // whose private half the serving server holds. Without it the client keeps
    // signing every request.
    void setSessionSealingKey(Bytes servingSealingKeyDer);

    // --- Account (service node) ---

    // Publishes our contact card, which is also what turns a portal
    // registration into an account: the first call redeems it. Called again
    // whenever the routing changes - there is no term to renew.
    //
    // sealingPrekeyDer, when non-empty, is published in the card so contacts can
    // E2E-encrypt their first message. ownDest is this profile's own destination
    // host: the card carries it while the server has not published the
    // destination yet (tunnels take minutes), since the address is the user's
    // master b32 either way.
    PublishResult publishCard(const Bytes& sealingPrekeyDer = {}, const std::string& ownDest = {});
    // Ends the account: the destination is revoked and everything the node holds
    // for it is dropped. Coming back means registering again.
    void closeAccount();
    // Onboarding discovery (GET /v1/account/portal): the server's message and
    // registration link(s), shown when subscribing is refused because the key is
    // not registered yet. Unauthenticated on the server; safe to call any time.
    PortalInfo fetchPortalInfo();
    // Hands the serving server a fresh offline transient (I2P-base64) so it can
    // operate the user's destination for the subscription window. An empty
    // transient revokes. Raises on refusal - a moderated node withholds the
    // destination until an operator approves the account.
    void sendI2pTransient(const std::string& transientB64, std::int64_t expiresUnix);
    // The per-user i2p-dest status (GET /v1/account/i2p-status).
    I2pDestStatus i2pStatus();
    // The user's own storage usage: the mailbox (GET /v1/messaging/storage-usage)
    // and the blob store (GET /v1/storage/usage), both through the same facade. Each
    // half is fetched independently; a backend that does not answer leaves its half
    // at zero with ok=false. Never throws - it is a best-effort status poll.
    StorageUsage storageUsage();
    // Own-server proxy relay (POST /v1/messaging/fetch): moves a sealed fetch
    // frame over I2P to toDest and returns the sealed reply opaquely. The
    // fallback FetchTransport for clients with no I2P transport of their own.
    FetchOutcome relayFetch(
        const std::string& toDest, const std::string& op, const Bytes& sealed);
    // First-contact card fetch from a descriptor (fp + serving destination +
    // serving sealing key). The query (which fingerprint) is sealed to the
    // serving server's key so a relay cannot read it; the response is sealed to a
    // fresh ephemeral key. The sealed frame is moved by `transport` (direct
    // transient-I2P, or the own-server proxy). Verifies the card and that it is
    // for the descriptor's fingerprint (see docs-main api/FederatedResolve.md).
    ContactInfo fetchCard(const Descriptor& descriptor, const FetchTransport& transport);
    // Resolves an alias to a descriptor via the central resolver: seals the query
    // (alias + ephemeral response key) to the resolver's serving key, moves it
    // with `transport` (op "resolve") to the resolver's destination, unseals the
    // reply, and verifies the signed record's chain against the resolver's root
    // fingerprint and `now`. Asserts the record is for the requested alias.
    // Throws on a transport error, ALIAS_UNKNOWN, or any verification failure.
    Descriptor resolveAlias(const std::string& alias, const ResolverCoordinate& resolver,
        std::int64_t now, const FetchTransport& transport);
    // This user's assigned serving destination + serving sealing key, from the
    // messaging server (GET /v1/messaging/destination).
    DestinationInfo myDestination();

    // --- Messaging (server) ---

    void registerThisClient();
    // This account's registered devices, and which one is us. Mail is deleted
    // only when every one of them has acked it.
    struct DeviceEntry {
        std::string clientId;
        bool current = false;
        // Mail this device has not acked yet: the one that stopped fetching is
        // the one holding the mailbox.
        std::size_t queued = 0;
    };
    std::vector<DeviceEntry> listClients();
    void retireClient(const std::string& clientId);
    void registerTokenHashes(const std::vector<Bytes>& hashes);
    void deleteTokenHashes(const std::vector<Bytes>& hashes);
    std::vector<PendingEntry> listPending();
    // Asks the server to hold the request until something arrives for this client
    // (or waitSeconds passes), and returns what is pending then. Throws with a 404
    // when the server has no event face, so the caller can fall back to polling.
    std::vector<PendingEntry> waitForPending(int waitSeconds);
    Bytes fetchBlob(const std::string& blobId);
    // A slice of the server's netDb, for starting I2P without touching a public
    // reseed host (GET /v1/messaging/reseed). Unauthenticated on the server side:
    // this runs before the client has any transport at all.
    std::vector<Bytes> fetchReseed();
    void ack(const std::string& blobId);
    // messageId is the delivery id (also sealed inside the envelope): sent in the
    // clear so our own server can correlate the recipient's signed delivered-ack
    // back to this attempt (the amber state).
    // Writes a blob into this account's own mailbox for its other devices. The
    // request's signature is the whole admission check: the caller owns the
    // mailbox, so there is no token to spend, no destination to dial and nothing
    // to federate - and it stays out of the tokenless budget, which is there to
    // bound strangers.
    void submitSelf(const std::string& messageId, const Bytes& payload);

    std::string submitSend(const std::string& toDest, const Bytes& sealed, const Bytes& payload,
        const std::string& messageId = {});
    SendStatus pollSend(const std::string& attemptId);

private:
    const Identity identity_;
    ApiClient api_;
};

}  // namespace bazarish::client
