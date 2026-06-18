// Bazarish project (c) 2026
#include "Client.hpp"

#include <bazarish/Auth.hpp>
#include <bazarish/Certificates.hpp>
#include <bazarish/Cms.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Errors.hpp>
#include <bazarish/Resolve.hpp>

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <stdexcept>
#include <string>
#include <thread>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

using namespace bazarish;
using namespace bazarish::client;

namespace {

std::int64_t nowSeconds()
{
    return static_cast<std::int64_t>(std::time(nullptr));
}

auth::Headers collectAuthHeaders(const httplib::Request& request)
{
    auth::Headers headers;
    for (const char* const name : {auth::kHeaderKeys, auth::kHeaderTimestamp,
             auth::kHeaderSignatureClassical, auth::kHeaderSignaturePq}) {
        if (request.has_header(name)) {
            headers[name] = request.get_header_value(name);
        }
    }
    return headers;
}

// Verifies the request signature against the real path and returns the
// caller fingerprint, mirroring the server-side authenticated() wrapper.
std::string requireCaller(const httplib::Request& request)
{
    return auth::verifyRequest(collectAuthHeaders(request), nowSeconds(), request.method,
        request.path, Bytes(request.body.begin(), request.body.end()));
}

void respondJson(httplib::Response& response, const nlohmann::json& body)
{
    response.set_content(body.dump(), "application/json");
}

}  // namespace

int main()
{
    const std::int64_t now = nowSeconds();

    const Identity serverIdentity = Identity::generate();
    const std::string serverFp = serverIdentity.fingerprint();
    const Key serverSealing = Key::generateSealing();
    // Destination-routed model: the server assigns each user a serving
    // destination + serving sealing key (here serverSealing stands in as that
    // key). Routing is by destination string, not by server fingerprint.
    const std::string aliceDest = "dlkbeyqjykssca6o7qlbwgq4fr2hry7kw2ursn2sh3lt3acox6gq.b32.i2p";
    const std::string bobDest = "elkbeyqjykssca6o7qlbwgq4fr2hry7kw2ursn2sh3lt3acox6gq.b32.i2p";

    // The central alias resolver: a root identity (the hardcoded trust anchor), a
    // short-lived delegated identity it signs records with, and its serving
    // sealing key + .b32.i2p destination.
    const Identity resolverRoot = Identity::generate();
    const Identity resolverDelegated = Identity::generate();
    const Key resolverSealing = Key::generateSealing();
    const std::string resolverDest = "flkbeyqjykssca6o7qlbwgq4fr2hry7kw2ursn2sh3lt3acox6gq.b32.i2p";
    const std::int64_t resolverWeek = 7 * 24 * 3600;
    const Bytes resolverDelegationDer
        = DelegationCertificate::issue(resolverRoot, resolverDelegated, now, now + resolverWeek);

    const Identity alice = Identity::generate();
    const Identity bob = Identity::generate();

    httplib::Server server;

    // --- Account stub ---

    const auto handleSubscribe
        = [&](const httplib::Request& request, httplib::Response& response) {
              const std::string user = requireCaller(request);
              const Bytes der
                  = fromBase64(nlohmann::json::parse(request.body).at("cert").get<std::string>());
              const SubscriptionCertificate cert = SubscriptionCertificate::verify(der);
              CHECK(cert.user == user);
              CHECK(cert.server == serverFp);
              // The client publishes its sealing prekey and the routing it
              // fetched from the messaging server in the certificate.
              CHECK(!cert.sealingPublicKeyDer.empty());
              CHECK(cert.dest == aliceDest);
              CHECK(cert.servingSealingKeyDer == serverSealing.publicDer());
              respondJson(response,
                  {
                      {"notAfter", cert.notAfter},
                      {"quotaBytes", 10 * 1024 * 1024},
                      {"maxTermSeconds", 14 * 24 * 3600},
                  });
          };
    server.Post("/v1/account/subscribe", handleSubscribe);
    server.Post("/v1/account/renew", handleSubscribe);

    server.Get("/v1/account/subscription",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            respondJson(response, {{"notAfter", now + 3600}, {"quotaBytes", 42}});
        });

    server.Get("/v1/account/contact",
        [&](const httplib::Request& request, httplib::Response& response) {
            CHECK(request.get_param_value("user") == bob.fingerprint());
            const Key bobSealing = Key::generateSealing();
            const Bytes subCert = SubscriptionCertificate::issue(bob, serverFp, now, now + 3600,
                bobSealing.publicDer(), bobDest, serverSealing.publicDer());
            respondJson(response,
                {{"user", bob.fingerprint()}, {"subscriptionCert", toBase64(subCert)}});
        });

    server.Get("/v1/messaging/destination",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            respondJson(response,
                {{"dest", aliceDest}, {"servingKey", toBase64(serverSealing.publicDer())}});
        });

    // First-contact card-fetch relay: unseal the query with our serving key,
    // build bob's contact card, seal the response to the query's response key.
    server.Post("/v1/messaging/fetch",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            const nlohmann::json req = nlohmann::json::parse(request.body);
            const std::string op = req.at("op").get<std::string>();
            const Bytes sealed = fromBase64(req.at("sealed").get<std::string>());

            if (op == "card") {
                CHECK(req.at("toDest") == bobDest);
                const Bytes queryBytes
                    = cms::unseal(sealed, Key::fromPrivatePem(serverSealing.privatePem()));
                const CardFetchQuery query
                    = cardFetchQueryFromJson(nlohmann::json::parse(queryBytes));
                CHECK(query.fingerprint == bob.fingerprint());
                const Key bobSealing = Key::generateSealing();
                const Bytes subCert = SubscriptionCertificate::issue(bob, serverFp, now, now + 3600,
                    bobSealing.publicDer(), bobDest, serverSealing.publicDer());
                const std::string respJson = toJson(CardFetchResponse{subCert}).dump();
                const Bytes sealedResp = cms::seal(Bytes(respJson.begin(), respJson.end()),
                    Key::fromPublicDer(query.responseKeyDer));
                respondJson(response, {{"ok", true}, {"sealed", toBase64(sealedResp)}});
                return;
            }

            // op == "resolve": the central resolver unseals the query with its
            // serving key, signs a self-verifying record (delegated key) and seals
            // it to the response key. An unknown alias answers ALIAS_UNKNOWN.
            CHECK(op == "resolve");
            CHECK(req.at("toDest") == resolverDest);
            const Bytes queryBytes
                = cms::unseal(sealed, Key::fromPrivatePem(resolverSealing.privatePem()));
            const ResolveQuery query = resolveQueryFromJson(nlohmann::json::parse(queryBytes));
            if (query.alias == "ghost") {
                respondJson(response, {{"ok", false}, {"errorCode", "ALIAS_UNKNOWN"}});
                return;
            }
            // "swap" exercises the client-side guard: the resolver answers a
            // record bound to a different alias than the one queried.
            const std::string recordAlias = query.alias == "swap" ? "other" : query.alias;
            const Descriptor descriptor{bob.fingerprint(), bobDest, serverSealing.publicDer()};
            const ResolveRecord record{recordAlias, descriptor, now, now + resolverWeek};
            const ResolveResponse resp{
                signResolveRecord(record, resolverDelegated), resolverDelegationDer};
            const std::string respJson = toJson(resp).dump();
            const Bytes sealedResp = cms::seal(Bytes(respJson.begin(), respJson.end()),
                Key::fromPublicDer(query.responseKeyDer));
            respondJson(response, {{"ok", true}, {"sealed", toBase64(sealedResp)}});
        });

    // --- Messaging stub ---

    server.Post("/v1/messaging/clients",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            CHECK(request.get_header_value("X-Bazarish-Client") == "client01");
            CHECK(nlohmann::json::parse(request.body).at("clientId") == "client01");
            respondJson(response, {{"ok", true}});
        });

    server.Post("/v1/messaging/tokens",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            CHECK(nlohmann::json::parse(request.body).at("hashes").size() == 2);
            respondJson(response, {{"ok", true}});
        });

    server.Get("/v1/messaging/pending",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            respondJson(response,
                {{"pending", nlohmann::json::array({{{"id", "blob1"}, {"class", "content"}},
                                {{"id", "blob2"}, {"class", "contact"}}})}});
        });

    server.Get("/v1/messaging/pending/blob1",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            response.set_content(std::string("\x01\x02\x03opaque", 9),
                "application/octet-stream");
        });

    server.Post("/v1/messaging/ack",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            CHECK(nlohmann::json::parse(request.body).at("blobId") == "blob1");
            respondJson(response, {{"ok", true}});
        });

    server.Post("/v1/messaging/send",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            const nlohmann::json body = nlohmann::json::parse(request.body);
            CHECK(body.at("toDest") == serverFp);
            // The sealed envelope must unseal and carry the expected fields.
            const Bytes sealed = fromBase64(body.at("sealed").get<std::string>());
            const Bytes plain = cms::unseal(sealed, serverSealing);
            const nlohmann::json inner = nlohmann::json::parse(plain.begin(), plain.end());
            CHECK(inner.at("class") == "content");
            CHECK(inner.at("mailbox") == bob.fingerprint());
            CHECK(inner.at("messageId") == "msg-1");
            CHECK(inner.contains("token"));
            respondJson(response, {{"attemptId", "deadbeef"}});
        });

    server.Get("/v1/messaging/send/deadbeef",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            respondJson(response,
                {{"status", "failed"},
                    {"error", {{"code", "STORAGE_FULL"}, {"message", "recipient full"}}}});
        });

    const int port = server.bind_to_any_port("127.0.0.1");
    CHECK(port > 0);
    std::thread serverThread([&server]() { (void)server.listen_after_bind(); });
    server.wait_until_ready();

    ServerEndpoint endpoint;
    endpoint.serverFingerprint = serverFp;
    endpoint.facades = {Facade{false, "127.0.0.1", port, {}}};

    Client client(Identity::fromPrivatePem(alice.privatePem()), "client01", endpoint);

    // The own-server proxy fetch transport (the no-SAM-bridge path): card and
    // resolve frames ride through POST /v1/messaging/fetch (relayFetch).
    const FetchTransport proxy
        = [&client](const std::string& toDest, const std::string& op, const Bytes& sealed) {
              return client.relayFetch(toDest, op, sealed);
          };

    // Subscribe (publishing a sealing prekey) returns the granted lifecycle
    // and a verifiable server card.
    {
        const Key aliceSealing = Key::generateSealing();
        const SubscribeResult result
            = client.subscribe(now, now + 7 * 24 * 3600, aliceSealing.publicDer());
        CHECK(result.notAfter == now + 7 * 24 * 3600);
        CHECK(result.quotaBytes == 10u * 1024 * 1024);
        CHECK(result.dest == aliceDest);
        CHECK(result.servingSealingKeyDer == serverSealing.publicDer());
    }

    // Contact lookup by fingerprint returns the verified certificate carrying
    // the prekey and the routing (dest + serving sealing key).
    {
        const ContactInfo looked = client.lookupContact(bob.fingerprint());
        CHECK(looked.subscriptionCert.user == bob.fingerprint());
        CHECK(looked.subscriptionCert.server == serverFp);
        CHECK(!looked.subscriptionCert.sealingPublicKeyDer.empty());
        CHECK(looked.subscriptionCert.dest == bobDest);
        CHECK(looked.subscriptionCert.servingSealingKey().publicDer() == serverSealing.publicDer());
    }

    // First-contact card fetch from a descriptor (via the own-server proxy): the
    // query fingerprint is sealed to the serving key, the verified card comes
    // back and must be for the descriptor's fingerprint.
    {
        const Descriptor descriptor{bob.fingerprint(), bobDest, serverSealing.publicDer()};
        const ContactInfo info = client.fetchCard(descriptor, proxy);
        CHECK(info.subscriptionCert.user == bob.fingerprint());
        CHECK(info.subscriptionCert.dest == bobDest);
        CHECK(info.subscriptionCert.servingSealingKey().publicDer() == serverSealing.publicDer());
    }

    // Subscription status.
    {
        const Subscription status = client.subscriptionStatus();
        CHECK(status.notAfter == now + 3600);
        CHECK(status.quotaBytes == 42u);
    }

    // Central alias resolve: the signed, self-verifying record maps the alias to a
    // descriptor; the chain is verified against the resolver root fingerprint.
    const ResolverCoordinate resolver{
        resolverRoot.fingerprint(), resolverDest, resolverSealing.publicDer()};
    const auto rejects = [&](const auto& fn) {
        try {
            fn();
        } catch (const std::exception&) {
            return true;
        }
        return false;
    };
    {
        const Descriptor descriptor = client.resolveAlias("bob", resolver, now, proxy);
        CHECK(descriptor.fingerprint == bob.fingerprint());
        CHECK(descriptor.srv == bobDest);
        CHECK(descriptor.srvKeyDer == serverSealing.publicDer());

        // An unknown alias surfaces as a thrown ALIAS_UNKNOWN.
        CHECK(rejects([&]() { (void)client.resolveAlias("ghost", resolver, now, proxy); }));

        // A record anchored to a different root is rejected (anti-MITM): even a
        // correctly-formed reply fails the chain check against our root.
        const ResolverCoordinate wrongRoot{
            Identity::generate().fingerprint(), resolverDest, resolverSealing.publicDer()};
        CHECK(rejects([&]() { (void)client.resolveAlias("bob", wrongRoot, now, proxy); }));

        // A record whose alias differs from the one queried is rejected.
        CHECK(rejects([&]() { (void)client.resolveAlias("swap", resolver, now, proxy); }));
    }

    // Client registry and token registration.
    {
        client.registerThisClient();
        client.registerTokenHashes({Bytes(32, 0x11), Bytes(32, 0x22)});
    }

    // Pending list, blob fetch and ack.
    {
        const std::vector<PendingEntry> pending = client.listPending();
        CHECK(pending.size() == 2);
        CHECK(pending[0].id == "blob1");
        CHECK(pending[0].deliveryClass == "content");
        CHECK(pending[1].deliveryClass == "contact");

        const Bytes blob = client.fetchBlob("blob1");
        CHECK(blob.size() == 9);
        CHECK(blob[0] == 0x01);

        client.ack("blob1");
    }

    // Build and submit a sealed message, then poll the typed failure.
    {
        const Key recipientSealing = Key::fromPublicDer(serverSealing.publicDer());
        const Bytes sealed = sealDeliveryEnvelope(
            "content", bob.fingerprint(), "msg-1", Bytes(32, 0x33), recipientSealing);
        const Bytes payload = {0x10, 0x20, 0x30};
        const std::string attemptId = client.submitSend(serverFp, sealed, payload);
        CHECK(attemptId == "deadbeef");

        const SendStatus status = client.pollSend(attemptId);
        CHECK(status.status == "failed");
        CHECK(status.errorCode.has_value());
        CHECK(status.errorCode.value() == ErrorCode::eStorageFull);
    }

    server.stop();
    serverThread.join();

    std::fprintf(stderr, "TestClient passed\n");
    return 0;
}
