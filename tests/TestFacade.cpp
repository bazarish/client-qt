// Bazarish project (c) 2026
#include "ApiClient.hpp"
#include "Invite.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

using namespace bazarish::client;

namespace {

void testParse()
{
    {
        const Facade f = parseFacadeUrl("http://example.com");
        CHECK(!f.tls);
        CHECK(f.host == "example.com");
        CHECK(f.port == 80);
        CHECK(f.basePath.empty());
    }
    {
        const Facade f = parseFacadeUrl("https://example.com");
        CHECK(f.tls);
        CHECK(f.port == 443);
    }
    {
        const Facade f = parseFacadeUrl("http://127.0.0.1:18482");
        CHECK(!f.tls);
        CHECK(f.host == "127.0.0.1");
        CHECK(f.port == 18482);
        CHECK(f.basePath.empty());
    }
    {
        const Facade f = parseFacadeUrl("https://relay.example.org:8443/s/9f3c");
        CHECK(f.tls);
        CHECK(f.host == "relay.example.org");
        CHECK(f.port == 8443);
        CHECK(f.basePath == "/s/9f3c");
    }
    {
        // Trailing slash on the base path is trimmed.
        const Facade f = parseFacadeUrl("http://h:1/path/");
        CHECK(f.basePath == "/path");
    }
    {
        // No scheme defaults to http.
        const Facade f = parseFacadeUrl("127.0.0.1:18482");
        CHECK(!f.tls);
        CHECK(f.port == 18482);
    }
}

void testFormatRoundTrip()
{
    const char* urls[] = {
        "http://example.com",
        "https://example.com",
        "http://127.0.0.1:18482",
        "https://relay.example.org:8443/s/9f3c",
        "https://host/path",
    };
    for (const char* url : urls) {
        const Facade f = parseFacadeUrl(url);
        // Re-parsing the formatted URL yields the same facade.
        const Facade again = parseFacadeUrl(facadeToUrl(f));
        CHECK(again.tls == f.tls);
        CHECK(again.host == f.host);
        CHECK(again.port == f.port);
        CHECK(again.basePath == f.basePath);
    }
    // The default port is omitted in the formatted form.
    CHECK(facadeToUrl(parseFacadeUrl("https://example.com:443")) == "https://example.com");
    CHECK(facadeToUrl(parseFacadeUrl("http://h:18482")) == "http://h:18482");
}

void testInvalid()
{
    const char* bad[] = {"http://", "ftp://host", "http://host:notaport", "https://:443"};
    for (const char* url : bad) {
        bool threw = false;
        try {
            parseFacadeUrl(url);
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
    }
}

void testEndpointFacades()
{
    ServerEndpoint endpoint;
    endpoint.serverFingerprint = "srvfp";
    endpoint.facades = {parseFacadeUrl("http://a:1"), parseFacadeUrl("https://b:2/x")};
    CHECK(endpoint.facades.size() == 2);
    CHECK(!endpoint.facades[0].tls);
    CHECK(endpoint.facades[0].host == "a");
    CHECK(endpoint.facades[1].tls);
    CHECK(endpoint.facades[1].basePath == "/x");
    CHECK(facadeToUrl(endpoint.facades[1]) == "https://b:2/x");
}

void testServerLink()
{
    ServerLink link;
    link.serverFingerprint = "abcdef0123456789";
    link.facadeUrls = {"https://relay.example.org:8443/s/9f3c", "http://127.0.0.1:18482"};
    const std::string uri = encodeServerLink(link);
    CHECK(uri.rfind("bazarish://server/", 0) == 0);

    const ServerLink back = decodeServerLink(uri);
    CHECK(back.serverFingerprint == link.serverFingerprint);
    CHECK(back.facadeUrls.size() == 2);
    CHECK(back.facadeUrls[0] == link.facadeUrls[0]);
    CHECK(back.facadeUrls[1] == link.facadeUrls[1]);

    // A contact invite URI is not a server link.
    bool threw = false;
    try {
        decodeServerLink("bazarish://invite/abc");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

// I2P facades (host ends in ".b32.i2p") are preferred over clearnet, but only
// when an I2P transport is configured; the active-facade reporting that drives
// the account list marking reflects this. No network is touched: constructing
// the client only computes the facade order (the router starts on first request).
void testI2pPriority()
{
    const bazarish::Identity id = bazarish::Identity::generate();

    {
        // I2P transport present: the I2P facade is preferred and reported active.
        ServerEndpoint endpoint;
        endpoint.serverFingerprint = "srvfp";
        endpoint.facades
            = {parseFacadeUrl("https://clear.example:8443"), parseFacadeUrl("http://abc.b32.i2p")};
        ApiClient api(id, "cid", endpoint, "/tmp/bazarish-test/i2p");
        CHECK(api.activeFacadeIsI2p());
        CHECK(api.activeFacadeUrl() == "http://abc.b32.i2p");
    }
    {
        // No I2P transport: the I2P facade is unreachable, so clearnet is active
        // and the connection is not falsely marked as I2P.
        ServerEndpoint endpoint;
        endpoint.serverFingerprint = "srvfp";
        endpoint.facades
            = {parseFacadeUrl("https://clear.example:8443"), parseFacadeUrl("http://abc.b32.i2p")};
        ApiClient api(id, "cid", endpoint, {});
        CHECK(!api.activeFacadeIsI2p());
        CHECK(api.activeFacadeUrl() == "https://clear.example:8443");
    }
    {
        // All-clearnet is never marked I2P, transport or not.
        ServerEndpoint endpoint;
        endpoint.serverFingerprint = "srvfp";
        endpoint.facades = {parseFacadeUrl("http://a:1"), parseFacadeUrl("https://b:2/x")};
        ApiClient api(id, "cid", endpoint, "/tmp/bazarish-test/i2p");
        CHECK(!api.activeFacadeIsI2p());
        CHECK(api.activeFacadeUrl() == "http://a:1");
    }
}

}  // namespace

int main()
{
    testParse();
    testFormatRoundTrip();
    testInvalid();
    testEndpointFacades();
    testServerLink();
    testI2pPriority();
    std::fprintf(stderr, "TestFacade passed\n");
    return 0;
}
