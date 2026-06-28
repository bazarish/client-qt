// Bazarish project (c) 2026
#pragma once

#include "ApiClient.hpp"
#include "BlobTransport.hpp"
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
    // Serving mode/state: "home", "pool", or for a personal destination
    // "building" / "active" / "expired" (see server-core PerUserDestinations).
    // The client only publishes a personal address while it is "active".
    std::string state;
};

// The per-user i2p-dest status the client polls (GET /v1/account/i2p-status):
// whether the option is on and paid-active, the recorded transient validity (so
// any of the user's devices knows when to re-issue), the effective storage
// quota, and honest projected shutoff dates from the prepaid balance.
struct I2pDestStatus {
    bool enabled = false;
    bool active = false;
    std::int64_t paidThrough = 0;
    std::int64_t projectedShutoff = 0;
    std::int64_t transientExpires = 0;
    std::int64_t transientUpdatedAt = 0;
    std::uint64_t storageQuotaBytes = 0;
    bool storageActive = false;
    std::int64_t storageProjectedShutoff = 0;
    std::string balanceAtomic;  // signed, as a string
    std::string currency;
};

// The user's own storage usage on its two backends, polled for the per-profile
// settings view. Each half has an `ok` flag: a backend that did not answer (e.g.
// offline) leaves its figures at zero with ok=false, so the UI can show the part
// that succeeded and mark the rest stale.
struct StorageUsage {
    bool mailboxOk = false;
    std::uint64_t mailboxUsedBytes = 0;
    std::uint64_t mailboxQuotaBytes = 0;
    bool blobOk = false;
    std::uint64_t blobUsedBytes = 0;
    std::uint64_t blobQuotaBytes = 0;
};

// Server reply to subscribe/renew: the granted lifecycle plus the serving
// destination + serving sealing key the certificate now carries.
struct SubscribeResult {
    std::int64_t notAfter = 0;
    std::uint64_t quotaBytes = 0;
    std::int64_t maxTermSeconds = 0;
    // The user's assigned serving destination and its serving sealing key.
    std::string dest;
    Bytes servingSealingKeyDer;
    // The subscription certificate we issued (DER): our self-signed contact
    // card (prekey + dest + serving sealing key), persisted so we can hand a
    // contact the full self-verifying card.
    Bytes subscriptionCertDer;
};

// A contact looked up by fingerprint: the user's subscription certificate,
// which carries the sealing prekey plus the routing (dest + serving sealing
// key). Verified before being returned.
struct ContactInfo {
    SubscriptionCertificate subscriptionCert;
};

struct Subscription {
    std::int64_t notAfter = 0;
    std::uint64_t quotaBytes = 0;
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

    // --- Account (service node) ---

    // sealingPrekeyDer, when non-empty, is published in the subscription
    // certificate so contacts can E2E-encrypt their first message.
    SubscribeResult subscribe(
        std::int64_t issuedAt, std::int64_t notAfter, const Bytes& sealingPrekeyDer = {});
    SubscribeResult renew(
        std::int64_t issuedAt, std::int64_t notAfter, const Bytes& sealingPrekeyDer = {});
    Subscription subscriptionStatus();
    void unsubscribe();
    // Onboarding discovery (GET /v1/account/portal): the server's message and
    // registration link(s), shown when subscribing is refused because the key is
    // not registered yet. Unauthenticated on the server; safe to call any time.
    PortalInfo fetchPortalInfo();
    // Hands the serving server a fresh offline transient (I2P-base64) so it can
    // operate the user's personal destination for the subscription window.
    // Requires an active i2pDest entitlement server-side; returns acceptance.
    bool sendI2pTransient(const std::string& transientB64, std::int64_t expiresUnix);
    // The per-user i2p-dest status (GET /v1/account/i2p-status).
    I2pDestStatus i2pStatus();
    // The user's own storage usage: the mailbox (GET /v1/messaging/storage-usage)
    // and the blob store (GET /v1/storage/usage), both through the same facade. Each
    // half is fetched independently; a backend that does not answer leaves its half
    // at zero with ok=false. Never throws - it is a best-effort status poll.
    StorageUsage storageUsage();
    // Turns the per-user i2p-dest option on or off (POST
    // /v1/account/i2p-dest/setting). Enabling charges a term server-side (or
    // resumes if still paid); returns whether the request was accepted (false
    // when enabling failed for an insufficient balance).
    bool setI2pDestEnabled(bool enabled);
    // Looks up a user by fingerprint: their subscription certificate (sealing
    // prekey + routing). Verified.
    ContactInfo lookupContact(const std::string& peerFingerprint);
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
    void retireClient(const std::string& clientId);
    void registerTokenHashes(const std::vector<Bytes>& hashes);
    void deleteTokenHashes(const std::vector<Bytes>& hashes);
    std::vector<PendingEntry> listPending();
    Bytes fetchBlob(const std::string& blobId);
    void ack(const std::string& blobId);
    // messageId is the delivery id (also sealed inside the envelope): sent in the
    // clear so our own server can correlate the recipient's signed delivered-ack
    // back to this attempt (the amber state).
    std::string submitSend(const std::string& toDest, const Bytes& sealed, const Bytes& payload,
        const std::string& messageId = {});
    SendStatus pollSend(const std::string& attemptId);

    // --- Large media: blob storage (rotating encrypted-LeaseSet, I2P) ---

    // Uploads a packed large blob's ciphertext to blob storage (PUT
    // /v1/storage/blob via the facade) with the given retention; returns the
    // capability fields for the sealed pointer.
    BlobUploadResult uploadBlob(const PackedBlob& packed, const BlobRetention& retention);
    // Streamed counterpart: uploads the ciphertext from a temp file without
    // holding it in memory (the large-file path).
    BlobUploadResult uploadBlobFromFile(const PackedBlobFile& packed,
        const BlobRetention& retention, const UploadProgressFn& onProgress = {});

    // Fetches a blob through our own server's I2P proxy (the fallback when this
    // client has no I2P transport of its own), verifying and decrypting it.
    Bytes fetchBlobViaProxy(const BlobPointer& pointer);

    // Deletes a blob (sender unsend) through our own server's I2P proxy.
    void deleteBlobViaProxy(const std::string& blobUrl, const std::string& deleteToken);

private:
    SubscribeResult submitSubscription(const std::string& path, std::int64_t issuedAt,
        std::int64_t notAfter, const Bytes& sealingPrekeyDer);

    const Identity identity_;
    ApiClient api_;
};

}  // namespace bazarish::client
