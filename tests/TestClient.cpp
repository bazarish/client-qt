// Bazarish project (c) 2026
#include "Client.hpp"

#include <bazarish/Auth.hpp>
#include <bazarish/Certificates.hpp>
#include <bazarish/Cms.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/ServerDescriptor.hpp>
#include <bazarish/Errors.hpp>
#include <bazarish/Resolve.hpp>

#include <bazarish/HttpServer.hpp>

#include <functional>
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

auth::Headers collectAuthHeaders(const http::Request& request)
{
    auth::Headers headers;
    for (const char* const name : {auth::kHeaderKeys, auth::kHeaderTimestamp,
             auth::kHeaderSignatureClassical, auth::kHeaderSignaturePq}) {
        if (request.hasHeader(name)) {
            headers[name] = request.header(name);
        }
    }
    return headers;
}

// Verifies the request signature against the real path and returns the
// caller fingerprint, mirroring the server-side authenticated() wrapper.
std::string requireCaller(const http::Request& request)
{
    return auth::verifyRequest(collectAuthHeaders(request), nowSeconds(), request.method,
        request.path, Bytes(request.body.begin(), request.body.end()));
}

// The tests write handlers the way the stub server used to take them - fill in
// a response - while the server hands one back; this bridges the two shapes.
using StubHandler = std::function<void(const http::Request&, http::Response&)>;

http::Handler stub(StubHandler handler)
{
    return [handler = std::move(handler)](const http::Request& request) {
        http::Response response;
        handler(request, response);
        return response;
    };
}

http::Server::Options localOptions()
{
    http::Server::Options options;
    options.port = 0;  // the kernel picks one
    return options;
}

void respondJson(http::Response& response, const nlohmann::json& body)
{
    response.contentType = "application/json";
    response.body = body.dump();
}

}  // namespace

// The stub server these tests talk to is a plain HTTP listener on localhost -
// the same shape as a stand on a LAN, and the reason that switch exists.
int main()
{
    bazarish::setAllowFacadeWithoutI2pForDevPurposes(true);
    const std::int64_t now = nowSeconds();

    const Identity serverIdentity = Identity::generate();
    const std::string serverFp = serverIdentity.fingerprint();
    const Key serverSealing = Key::generateSealing();
    // Destination-routed model: the server assigns each user a serving
    // destination + serving sealing key (here serverSealing stands in as that
    // key). Routing is by destination string, not by server fingerprint.
    const std::string aliceDest = "dlkbeyqjykssca6o7qlbwgq4fr2hry7kw2ursn2sh3lt3acox6gq.b32.i2p";
    const std::string bobDest = "elkbeyqjykssca6o7qlbwgq4fr2hry7kw2ursn2sh3lt3acox6gq.b32.i2p";
    const std::string bobView = "0123456789abcdef0123456789abcdef";

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

    http::Server server(localOptions());

    // --- Account stub ---

    server.post("/v1/account/card",
        stub([&](const http::Request& request, http::Response& response) {
            const std::string user = requireCaller(request);
            const Bytes der
                = fromBase64(nlohmann::json::parse(request.body).at("card").get<std::string>());
            const ContactCard card = ContactCard::verify(der);
            CHECK(card.user == user);
            // The card names its owner and their routing, and nothing else - a
            // contact must not learn which server operates the destination.
            CHECK(!card.sealingPublicKeyDer.empty());
            CHECK(card.dest == aliceDest);
            CHECK(card.servingSealingKeyDer == serverSealing.publicDer());
            respondJson(response, {{"quotaBytes", 10 * 1024 * 1024}});
        }));

    server.get("/v1/messaging/destination",
        stub([&](const http::Request& request, http::Response& response) {
            (void)requireCaller(request);
            respondJson(response,
                {{"dest", aliceDest}, {"servingKey", toBase64(serverSealing.publicDer())}});
        }));

    // First-contact card-fetch relay: unseal the query with our serving key,
    // build bob's contact card, seal the response to the query's response key.
    server.post("/v1/messaging/fetch",
        stub([&](const http::Request& request, http::Response& response) {
            (void)requireCaller(request);
            const nlohmann::json req = nlohmann::json::parse(request.body);
            const std::string op = req.at("op").get<std::string>();
            const Bytes sealed = fromBase64(req.at("sealed").get<std::string>());

            if (op == "card") {
                CHECK(req.at("toDest") == bobDest);
                // The query travels in the clear: the stream it rides is already
                // encrypted to the destination, and nothing relays it.
                const CardFetchQuery query = cardFetchQueryFromJson(nlohmann::json::parse(sealed));
                CHECK(query.fingerprint == bob.fingerprint());
                // The asker brings back the capability from the descriptor;
                // without it no card is served.
                CHECK(query.view == bobView);
                const Key bobSealing = Key::generateSealing();
                const Bytes bobCard = ContactCard::issue(
                    bob, bobDest, bobSealing.publicDer(), serverSealing.publicDer());
                respondJson(response, {{"ok", true}, {"sealed", toBase64(bobCard)}});
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
            const Descriptor descriptor{bob.fingerprint(), bobDest, bobView};
            const ResolveRecord record{recordAlias, descriptor, now, now + resolverWeek};
            const ResolveResponse resp{
                signResolveRecord(record, resolverDelegated), resolverDelegationDer};
            const std::string respJson = toJson(resp).dump();
            const Bytes sealedResp = cms::seal(Bytes(respJson.begin(), respJson.end()),
                Key::fromPublicDer(query.responseKeyDer));
            respondJson(response, {{"ok", true}, {"sealed", toBase64(sealedResp)}});
        }));

    // --- Messaging stub ---

    server.post("/v1/messaging/clients",
        stub([&](const http::Request& request, http::Response& response) {
            (void)requireCaller(request);
            CHECK(request.header("X-Bazarish-Client") == "client01");
            CHECK(nlohmann::json::parse(request.body).at("clientId") == "client01");
            respondJson(response, {{"ok", true}});
        }));

    server.post("/v1/messaging/tokens",
        stub([&](const http::Request& request, http::Response& response) {
            (void)requireCaller(request);
            CHECK(nlohmann::json::parse(request.body).at("hashes").size() == 2);
            respondJson(response, {{"ok", true}});
        }));

    server.get("/v1/messaging/pending",
        stub([&](const http::Request& request, http::Response& response) {
            (void)requireCaller(request);
            respondJson(response,
                {{"pending", nlohmann::json::array({{{"id", "blob1"}, {"class", "content"}},
                                {{"id", "blob2"}, {"class", "contact"}}})}});
        }));

    server.get("/v1/messaging/pending/blob1",
        stub([&](const http::Request& request, http::Response& response) {
            (void)requireCaller(request);
            response.contentType = "application/octet-stream";
            response.body = std::string("\x01\x02\x03opaque", 9);
        }));

    server.post("/v1/messaging/ack",
        stub([&](const http::Request& request, http::Response& response) {
            (void)requireCaller(request);
            CHECK(nlohmann::json::parse(request.body).at("blobId") == "blob1");
            respondJson(response, {{"ok", true}});
        }));

    const int port = server.start();
    CHECK(port > 0);

    ServerEndpoint endpoint;
    endpoint.serverFingerprint = serverFp;
    endpoint.facades = {Facade{false, "127.0.0.1", port, {}}};

    Client client(Identity::fromPrivatePem(alice.privatePem()), "client01", endpoint);

    // The own-server proxy fetch transport (the path for a client with no I2P
    // transport of its own): card and
    // resolve frames ride through POST /v1/messaging/fetch (relayFetch).
    const FetchTransport proxy
        = [&client](const std::string& toDest, const std::string& op, const Bytes& sealed) {
              return client.relayFetch(toDest, op, sealed);
          };

    // Publishing a card returns what the account holds and the routing the
    // messaging server assigned.
    {
        const Key aliceSealing = Key::generateSealing();
        const PublishResult result = client.publishCard(aliceSealing.publicDer());
        CHECK(result.quotaBytes == 10u * 1024 * 1024);
        CHECK(result.dest == aliceDest);
        CHECK(result.servingSealingKeyDer == serverSealing.publicDer());
        CHECK(!result.cardDer.empty());
    }

    // First-contact card fetch from a descriptor (via the own-server proxy): the
    // query carries the descriptor's key, the verified card comes back and must
    // be for the descriptor's fingerprint - and names no server.
    {
        const Descriptor descriptor{bob.fingerprint(), bobDest, bobView};
        const ContactInfo info = client.fetchCard(descriptor, proxy);
        CHECK(info.card.user == bob.fingerprint());
        CHECK(info.card.dest == bobDest);
        CHECK(info.card.servingSealingKey().publicDer() == serverSealing.publicDer());
        CHECK(!info.card.sealingPublicKeyDer.empty());
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
        CHECK(descriptor.dest == bobDest);
        CHECK(descriptor.view == bobView);

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

    // The envelope a delivery is carried in. It goes out over I2P from this
    // client, not through this server, so what matters here is that it seals to
    // the recipient destination's serving key and names the delivery.
    {
        const Key recipientSealing = Key::fromPublicDer(serverSealing.publicDer());
        const Bytes sealed = sealDeliveryEnvelope(
            "content", bob.fingerprint(), "msg-1", Bytes(32, 0x33), recipientSealing);
        const Bytes plain = cms::unseal(sealed, serverSealing);
        const nlohmann::json inner = nlohmann::json::parse(plain.begin(), plain.end());
        CHECK(inner.at("class") == "content");
        CHECK(inner.at("mailbox") == bob.fingerprint());
        CHECK(inner.at("deliveryId") == "msg-1");
        CHECK(inner.contains("token"));
    }

    server.stop();

    std::fprintf(stderr, "TestClient passed\n");
    // The name one envelope is called by: the same message to the same mailbox
    // keeps it, so a resend is the delivery the recipient's server already has.
    {
        const std::string key = "0123456789abcdef0123456789abcdef";
        const std::string first = deliveryIdFor(key, "msg-1", "mailbox-a");
        CHECK(first.size() == 32);
        CHECK(deliveryIdFor(key, "msg-1", "mailbox-a") == first);  // a resend
        // The copy that goes elsewhere - our own devices, another contact - is
        // named differently, so two servers holding them can match nothing.
        CHECK(deliveryIdFor(key, "msg-1", "mailbox-b") != first);
        CHECK(deliveryIdFor(key, "msg-2", "mailbox-a") != first);
        // And nobody without the account's secret can work out what a message of
        // theirs will be called.
        CHECK(deliveryIdFor("another-secret", "msg-1", "mailbox-a") != first);
    }

    return 0;
}
