// Bazarish project (c) 2026
#include "Client.hpp"

#include <bazarish/Auth.hpp>
#include <bazarish/Certificates.hpp>
#include <bazarish/Cms.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Errors.hpp>

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
    const Bytes serverCardDer = ServerCard::issue(serverIdentity, {"i2p:exampledest"},
        Key::fromPublicDer(serverSealing.publicDer()), now);

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
              // The client publishes its sealing prekey in the certificate.
              CHECK(!cert.sealingPublicKeyDer.empty());
              respondJson(response,
                  {
                      {"notAfter", cert.notAfter},
                      {"quotaBytes", 10 * 1024 * 1024},
                      {"maxTermSeconds", 14 * 24 * 3600},
                      {"serverCard", toBase64(serverCardDer)},
                  });
          };
    server.Post("/v1/account/subscribe", handleSubscribe);
    server.Post("/v1/account/renew", handleSubscribe);

    server.Get("/v1/account/subscription",
        [&](const httplib::Request& request, httplib::Response& response) {
            (void)requireCaller(request);
            respondJson(response, {{"notAfter", now + 3600}, {"quotaBytes", 42}});
        });

    server.Post("/v1/account/alias",
        [&](const httplib::Request& request, httplib::Response& response) {
            const std::string user = requireCaller(request);
            const Bytes der
                = fromBase64(nlohmann::json::parse(request.body).at("cert").get<std::string>());
            const AliasCertificate cert = AliasCertificate::verify(der);
            CHECK(cert.user == user);
            CHECK(cert.alias == "alice");
            respondJson(response, {{"ok", true}, {"alias", cert.alias}});
        });

    server.Get("/v1/account/resolve",
        [&](const httplib::Request& request, httplib::Response& response) {
            CHECK(request.get_param_value("alias") == "bob");
            const Bytes aliasCert = AliasCertificate::issue(bob, "bob", now, std::nullopt);
            const Bytes subCert
                = SubscriptionCertificate::issue(bob, serverFp, now, now + 3600);
            respondJson(response,
                {
                    {"user", bob.fingerprint()},
                    {"aliasCert", toBase64(aliasCert)},
                    {"subscriptionCert", toBase64(subCert)},
                });
        });

    server.Get("/v1/account/contact",
        [&](const httplib::Request& request, httplib::Response& response) {
            CHECK(request.get_param_value("user") == bob.fingerprint());
            const Key bobSealing = Key::generateSealing();
            const Bytes subCert = SubscriptionCertificate::issue(
                bob, serverFp, now, now + 3600, bobSealing.publicDer());
            respondJson(response, {{"user", bob.fingerprint()},
                                      {"subscriptionCert", toBase64(subCert)},
                                      {"serverCard", toBase64(serverCardDer)}});
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
                {{"pending", nlohmann::json::array({{{"id", "blob1"}, {"kind", "message"}},
                                {{"id", "blob2"}, {"kind", "contact-request"}}})}});
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
            CHECK(body.at("toServer") == serverFp);
            // The sealed envelope must unseal and carry the expected fields.
            const Bytes sealed = fromBase64(body.at("sealed").get<std::string>());
            const Bytes plain = cms::unseal(sealed, serverSealing);
            const nlohmann::json inner = nlohmann::json::parse(plain.begin(), plain.end());
            CHECK(inner.at("kind") == "message");
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
    endpoint.host = "127.0.0.1";
    endpoint.port = port;
    endpoint.serverFingerprint = serverFp;

    Client client(Identity::fromPrivatePem(alice.privatePem()), "client01", endpoint);

    // Subscribe (publishing a sealing prekey) returns the granted lifecycle
    // and a verifiable server card.
    {
        const Key aliceSealing = Key::generateSealing();
        const SubscribeResult result
            = client.subscribe(now, now + 7 * 24 * 3600, aliceSealing.publicDer());
        CHECK(result.notAfter == now + 7 * 24 * 3600);
        CHECK(result.quotaBytes == 10u * 1024 * 1024);
        CHECK(result.serverCard.server == serverFp);
        CHECK(result.serverCard.sealingPublicKeyDer == serverSealing.publicDer());
    }

    // Contact lookup by fingerprint returns the verified prekey certificate
    // and the serving server's card.
    {
        const ContactInfo looked = client.lookupContact(bob.fingerprint());
        CHECK(looked.subscriptionCert.user == bob.fingerprint());
        CHECK(looked.subscriptionCert.server == serverFp);
        CHECK(!looked.subscriptionCert.sealingPublicKeyDer.empty());
        CHECK(looked.serverCard.server == serverFp);
    }

    // Subscription status.
    {
        const Subscription status = client.subscriptionStatus();
        CHECK(status.notAfter == now + 3600);
        CHECK(status.quotaBytes == 42u);
    }

    // Alias registration signs an alias certificate for the caller.
    {
        client.registerAlias("alice", now, std::nullopt);
    }

    // Resolve returns a verified trust chain.
    {
        const ResolveResult resolved = client.resolve("bob");
        CHECK(resolved.user == bob.fingerprint());
        CHECK(resolved.aliasCert.alias == "bob");
        CHECK(resolved.subscriptionCert.server == serverFp);
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
        CHECK(pending[0].kind == "message");
        CHECK(pending[1].kind == "contact-request");

        const Bytes blob = client.fetchBlob("blob1");
        CHECK(blob.size() == 9);
        CHECK(blob[0] == 0x01);

        client.ack("blob1");
    }

    // Build and submit a sealed message, then poll the typed failure.
    {
        const Key recipientSealing = Key::fromPublicDer(serverSealing.publicDer());
        const Bytes sealed = sealDeliveryEnvelope(
            "message", bob.fingerprint(), "msg-1", Bytes(32, 0x33), recipientSealing);
        const Bytes payload = {0x10, 0x20, 0x30};
        const std::string attemptId = client.submitSend(serverFp, sealed, payload);
        CHECK(attemptId == "deadbeef");

        const SendStatus status = client.pollSend(attemptId);
        CHECK(status.status == "failed");
        CHECK(status.errorCode.has_value());
        CHECK(status.errorCode.value() == ErrorCode::kStorageFull);
    }

    server.stop();
    serverThread.join();

    std::fprintf(stderr, "TestClient passed\n");
    return 0;
}
