// Bazarish project (c) 2026
#include "ApiClient.hpp"
#include "BlobTransport.hpp"
#include "LargeBlob.hpp"

#include <bazarish/Bytes.hpp>

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

int main()
{
    std::string gotBody;
    std::string gotSha;
    std::string gotTtl;
    std::string gotCount;
    httplib::Server store;
    store.Put("/v1/storage/blob", [&](const httplib::Request& request, httplib::Response& response) {
        gotBody = request.body;
        gotSha = request.get_header_value("X-Blob-Sha256");
        gotTtl = request.get_header_value("X-Blob-Ttl");
        gotCount = request.get_header_value("X-Blob-Count");
        const nlohmann::json out = {
            {"blobUrl", "http://yhfjbu7hkuqyqwp3pvpuf6vk63bdlwtryjcueunavz52ai5y3yuvwdkh.b32.i2p/b/cap1"},
            {"blobId", "cap1"},
            {"deleteToken", "tok1"},
        };
        response.set_content(out.dump(), "application/json");
    });
    // Stand-in for the own-server proxy: serves at most 9000 bytes of proxyCipher
    // per call (forcing resume), with X-Blob-Total carrying the full length and a
    // Range param ("bytes=N-") continuing from an offset.
    std::string proxyCipher;
    store.Get("/v1/messaging/blob-proxy",
        [&](const httplib::Request& request, httplib::Response& response) {
            std::size_t offset = 0;
            if (request.has_param("range")) {
                const std::string range = request.get_param_value("range");
                const std::size_t eq = range.find('=');
                const std::size_t dash = range.find('-');
                offset = static_cast<std::size_t>(std::stoull(range.substr(eq + 1, dash - eq - 1)));
            }
            response.set_header("X-Blob-Total", std::to_string(proxyCipher.size()));
            const std::size_t chunk = std::min<std::size_t>(9000, proxyCipher.size() - offset);
            response.set_content(proxyCipher.substr(offset, chunk), "application/octet-stream");
        });
    const int port = store.bind_to_any_port("127.0.0.1");
    CHECK(port > 0);
    std::thread storeThread([&store]() { (void)store.listen_after_bind(); });
    while (!store.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const Identity alice = Identity::generate();
    ServerEndpoint endpoint;
    endpoint.serverFingerprint = "unused-here";
    endpoint.facades = {Facade{false, "127.0.0.1", port, {}}};
    ApiClient api(alice, "client1", endpoint);

    Bytes blob;
    for (int i = 0; i < 2048; ++i) {
        blob.push_back(static_cast<unsigned char>(i));
    }
    const PackedBlob packed = packLargeBlob(blob);

    // Count-based upload: ciphertext, digest and retention reach the store.
    {
        BlobRetention retention;
        retention.ttlSeconds = 3600;
        retention.count = 1;
        const BlobUploadResult result = uploadBlob(api, packed, retention);
        CHECK(result.blobId == "cap1");
        CHECK(result.deleteToken == "tok1");
        CHECK(result.blobUrl.find(".b32.i2p") != std::string::npos);
        CHECK(Bytes(gotBody.begin(), gotBody.end()) == packed.ciphertext);
        CHECK(gotSha == packed.sha256);
        CHECK(gotTtl == "3600");
        CHECK(gotCount == "1");
    }

    // TTL-only upload omits the count header (and an unset ttl omits its header).
    {
        gotTtl = "x";
        gotCount = "x";
        BlobRetention retention;  // ttl 0, no count
        (void)uploadBlob(api, packed, retention);
        CHECK(gotTtl.empty());
        CHECK(gotCount.empty());
        CHECK(gotSha == packed.sha256);
    }

    // --- Resume driver: reassembles a download across truncated transfers ---

    Bytes full;
    for (int i = 0; i < 100000; ++i) {
        full.push_back(static_cast<unsigned char>(i * 7 + 1));
    }
    const std::uint64_t total = full.size();

    // A clean single-shot 200 returns the whole ciphertext unchanged.
    {
        const RangedGetFn whole = [&](const std::uint64_t offset) -> RangedGet {
            RangedGet out;
            out.status = 200;
            out.total = total;
            out.body = Bytes(full.begin() + static_cast<std::ptrdiff_t>(offset), full.end());
            return out;
        };
        CHECK(downloadWithResume(whole) == full);
    }

    // A store that drops the stream after at most 9000 bytes per attempt (the
    // first attempt is a truncated 200, the rest are ranged 206s) is resumed to a
    // byte-exact whole.
    {
        int calls = 0;
        const RangedGetFn flaky = [&](const std::uint64_t offset) -> RangedGet {
            ++calls;
            RangedGet out;
            out.status = (offset == 0) ? 200 : 206;
            out.total = total;
            const std::size_t chunk
                = std::min<std::size_t>(9000, full.size() - static_cast<std::size_t>(offset));
            out.body = Bytes(full.begin() + static_cast<std::ptrdiff_t>(offset),
                full.begin() + static_cast<std::ptrdiff_t>(offset) + static_cast<std::ptrdiff_t>(chunk));
            return out;
        };
        const Bytes assembled = downloadWithResume(flaky);
        CHECK(assembled == full);
        CHECK(calls > 1);  // it actually resumed rather than fetching in one shot
    }

    // A transport that never makes progress gives up instead of looping forever.
    {
        const RangedGetFn dead
            = [](std::uint64_t) -> RangedGet { throw std::runtime_error("tunnel build failed"); };
        bool threw = false;
        try {
            (void)downloadWithResume(dead);
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
    }

    // A 206 without a parseable total is a protocol error, not silent truncation.
    {
        const RangedGetFn bogus = [&](const std::uint64_t offset) -> RangedGet {
            RangedGet out;
            out.status = 206;
            out.total = 0;  // missing Content-Range total
            out.body = Bytes(full.begin() + static_cast<std::ptrdiff_t>(offset), full.end());
            return out;
        };
        bool threw = false;
        try {
            (void)downloadWithResume(bogus);
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
    }

    // Proxy resume: the own server streams the ciphertext back in truncated
    // windows; fetchBlobViaProxy resumes through it and reassembles, then decrypts.
    {
        Bytes bigBlob;
        for (int i = 0; i < 50000; ++i) {
            bigBlob.push_back(static_cast<unsigned char>(i * 3 + 7));
        }
        const PackedBlob bigPacked = packLargeBlob(bigBlob);
        proxyCipher.assign(bigPacked.ciphertext.begin(), bigPacked.ciphertext.end());
        CHECK(proxyCipher.size() > 9000);  // spans several windows, so resume is exercised

        BlobPointer pointer;
        pointer.blobUrl = "http://yhfjbu7hkuqyqwp3pvpuf6vk63bdlwtryjcueunavz52ai5y3yuvwdkh.b32.i2p/b/cap1";
        pointer.blobId = "cap1";
        pointer.fileKey = bigPacked.fileKey;
        pointer.sha256 = bigPacked.sha256;
        pointer.size = bigBlob.size();

        const Bytes recovered = fetchBlobViaProxy(api, pointer);
        CHECK(recovered == bigBlob);
    }

    store.stop();
    storeThread.join();
    std::printf("TestBlobTransport: all checks passed\n");
    return 0;
}
