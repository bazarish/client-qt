// Bazarish project (c) 2026
#include "ApiClient.hpp"

#include <bazarish/Auth.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Errors.hpp>

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <stdexcept>
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

}  // namespace

int main()
{
    const Identity alice = Identity::generate();

    httplib::Server server;

    // Echoes the verified caller fingerprint and the client header, proving
    // the request was correctly signed against the query-less path.
    server.Get("/v1/account/subscription",
        [&](const httplib::Request& request, httplib::Response& response) {
            std::string user;
            try {
                user = auth::verifyRequest(collectAuthHeaders(request), nowSeconds(), "GET",
                    request.path, Bytes(request.body.begin(), request.body.end()));
            } catch (const std::exception& error) {
                response.status = 401;
                response.set_content(error.what(), "text/plain");
                return;
            }
            response.set_content(nlohmann::json{{"notAfter", 1234},
                                     {"quotaBytes", 10}, {"user", user}}
                                     .dump(),
                "application/json");
        });

    // A request signed for a secret base path must verify against the
    // stripped path, so the route lives under the base path but the auth
    // check uses the suffix.
    server.Post("/s/secret/v1/messaging/clients",
        [&](const httplib::Request& request, httplib::Response& response) {
            std::string user;
            try {
                user = auth::verifyRequest(collectAuthHeaders(request), nowSeconds(), "POST",
                    "/v1/messaging/clients", Bytes(request.body.begin(), request.body.end()));
            } catch (const std::exception& error) {
                response.status = 401;
                response.set_content(error.what(), "text/plain");
                return;
            }
            CHECK(request.get_header_value("X-Bazarish-Client") == "abc123");
            const nlohmann::json body = nlohmann::json::parse(request.body);
            CHECK(body.at("clientId") == "abc123");
            response.set_content(nlohmann::json{{"ok", true}, {"user", user}}.dump(),
                "application/json");
        });

    // Returns a typed error envelope.
    server.Get("/v1/account/resolve",
        [&](const httplib::Request&, httplib::Response& response) {
            response.status = 404;
            response.set_content(
                makeErrorEnvelope(ErrorCode::eAliasUnknown, "no such alias").dump(),
                "application/json");
        });

    // Returns a non-envelope error body.
    server.Get("/v1/messaging/pending",
        [&](const httplib::Request&, httplib::Response& response) {
            response.status = 500;
            response.set_content("internal boom", "text/plain");
        });

    const int port = server.bind_to_any_port("127.0.0.1");
    CHECK(port > 0);
    std::thread serverThread([&server]() { (void)server.listen_after_bind(); });
    server.wait_until_ready();

    ServerEndpoint endpoint;
    endpoint.serverFingerprint = "unused-here";
    endpoint.facades = {Facade{false, "127.0.0.1", port, {}}};

    // A signed GET round-trips and the server derives alice's fingerprint
    // from the presented keys.
    {
        ApiClient api(alice, "abc123", endpoint);
        const ApiResponse response = api.get("/v1/account/subscription");
        CHECK(response.status == 200);
        const nlohmann::json body = response.json();
        CHECK(body.at("notAfter") == 1234);
        CHECK(body.at("user") == alice.fingerprint());
    }

    // The same client over a secret base path: the URL carries the prefix,
    // the signature is computed over the stripped path.
    {
        ServerEndpoint secret = endpoint;
        secret.facades[0].basePath = "/s/secret";
        ApiClient api(alice, "abc123", secret);
        const ApiResponse response = api.postJson("/v1/messaging/clients",
            {{"clientId", "abc123"}});
        CHECK(response.status == 200);
        CHECK(response.json().at("user") == alice.fingerprint());
    }

    // A typed error envelope surfaces as ApiError carrying the code.
    {
        ApiClient api(alice, "abc123", endpoint);
        bool threw = false;
        try {
            api.get("/v1/account/resolve", "alias=ghost");
        } catch (const ApiError& error) {
            threw = true;
            CHECK(error.httpStatus == 404);
            CHECK(error.code.has_value());
            CHECK(error.code.value() == ErrorCode::eAliasUnknown);
        }
        CHECK(threw);
    }

    // A non-envelope error keeps the status but carries no typed code.
    {
        ApiClient api(alice, "abc123", endpoint);
        bool threw = false;
        try {
            api.get("/v1/messaging/pending");
        } catch (const ApiError& error) {
            threw = true;
            CHECK(error.httpStatus == 500);
            CHECK(!error.code.has_value());
        }
        CHECK(threw);
    }

    server.stop();
    serverThread.join();

    // A transport failure (nothing listening) is an ApiError with no HTTP
    // status and no typed code.
    {
        ServerEndpoint dead = endpoint;
        dead.facades[0].port = 1;  // Reserved; connection refused.
        ApiClient api(alice, "abc123", dead);
        bool threw = false;
        try {
            api.get("/v1/account/subscription");
        } catch (const ApiError& error) {
            threw = true;
            CHECK(error.httpStatus == 0);
            CHECK(!error.code.has_value());
        }
        CHECK(threw);
    }

    std::fprintf(stderr, "TestApiClient passed\n");
    return 0;
}
