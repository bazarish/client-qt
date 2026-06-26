// Bazarish project (c) 2026
#include "BlobTransport.hpp"

#include <bazarish/Cms.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/I2p.hpp>
#include <bazarish/I2pHttp.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace bazarish::client {

namespace {

constexpr const char* kB32Suffix = ".b32.i2p";

std::uint64_t headerUint64(const std::map<std::string, std::string>& headers,
    const std::string& key, const std::uint64_t fallback)
{
    const auto it = headers.find(key);
    if (it == headers.end()) {
        return fallback;
    }
    try {
        return static_cast<std::uint64_t>(std::stoull(it->second));
    } catch (const std::exception&) {
        return fallback;
    }
}

// Total length from a Content-Range value ("bytes <start>-<end>/<total>"); 0 if
// it is absent or unparseable.
std::uint64_t contentRangeTotal(const std::map<std::string, std::string>& headers)
{
    const auto it = headers.find("content-range");
    if (it == headers.end()) {
        return 0;
    }
    const std::size_t slash = it->second.rfind('/');
    if (slash == std::string::npos) {
        return 0;
    }
    try {
        return static_cast<std::uint64_t>(std::stoull(it->second.substr(slash + 1)));
    } catch (const std::exception&) {
        return 0;
    }
}

// Drives ranged GETs against get(), assembling the full ciphertext. A 200 means
// "the whole object from zero" (replace what we have); a 206 means "the bytes
// from our current offset" (append). A truncated transfer (an I2P stream drop)
// leaves a short body with no exception; the driver resumes from where it
// stopped. Bounded by consecutive attempts that make no forward progress. The
// assembly target is abstracted (sizeNow / onReplace / onAppend) so the same
// logic serves an in-memory buffer and a streamed on-disk sink.
void runResume(const RangedGetFn& get, const std::function<std::uint64_t()>& sizeNow,
    const std::function<void(Bytes&&)>& onReplace, const std::function<void(const Bytes&)>& onAppend)
{
    constexpr int kMaxStalledAttempts = 5;
    bool haveTotal = false;
    std::uint64_t total = 0;
    int stalled = 0;
    while (!haveTotal || sizeNow() < total) {
        const std::uint64_t before = sizeNow();
        RangedGet attempt;
        try {
            attempt = get(before);
        } catch (const std::exception&) {
            if (++stalled >= kMaxStalledAttempts) {
                throw;
            }
            continue;
        }
        if (attempt.status == 200) {
            // The store served the whole object (e.g. it ignored the Range
            // header); take it as the authoritative full ciphertext.
            total = attempt.total > 0 ? attempt.total : attempt.body.size();
            onReplace(std::move(attempt.body));
            haveTotal = true;
        } else if (attempt.status == 206) {
            if (attempt.total == 0) {
                throw std::runtime_error("blob store returned 206 without a total length");
            }
            total = attempt.total;
            onAppend(attempt.body);
            haveTotal = true;
        } else {
            throw std::runtime_error(
                "blob download failed: status " + std::to_string(attempt.status));
        }
        if (sizeNow() <= before) {
            if (++stalled >= kMaxStalledAttempts) {
                throw std::runtime_error("blob download stalled with no progress");
            }
        } else {
            stalled = 0;
        }
    }
}

// A resume sink that streams the assembled ciphertext to a file rather than an
// in-memory buffer, so a multi-gigabyte blob never sits whole in RAM. A 200
// (full object) truncates and rewrites; a 206 appends.
class FileSink {
public:
    explicit FileSink(std::filesystem::path path)
        : path_(std::move(path))
    {
        open(std::ios::trunc);
    }
    std::uint64_t size() const
    {
        return size_;
    }
    void replace(const Bytes& body)
    {
        open(std::ios::trunc);
        write(body);
    }
    void append(const Bytes& body)
    {
        write(body);
    }
    void finish()
    {
        out_.flush();
        if (!out_) {
            throw std::runtime_error("flushing temp blob file failed: " + path_.string());
        }
        out_.close();
    }

private:
    void open(const std::ios::openmode mode)
    {
        out_.close();
        out_.clear();
        out_.open(path_, std::ios::binary | mode);
        if (!out_) {
            throw std::runtime_error("cannot open temp blob file: " + path_.string());
        }
        size_ = 0;
    }
    void write(const Bytes& body)
    {
        out_.write(reinterpret_cast<const char*>(body.data()),
            static_cast<std::streamsize>(body.size()));
        if (!out_) {
            throw std::runtime_error("writing temp blob file failed: " + path_.string());
        }
        size_ += body.size();
    }
    std::filesystem::path path_;
    std::ofstream out_;
    std::uint64_t size_ = 0;
};

// Builds a ranged-GET function that fetches the blob over a fresh throwaway
// destination per attempt, asking for "bytes=<offset>-" once past the start so a
// dropped stream resumes instead of restarting. The router outlives every use
// of the returned function (it is driven within the fetch call).
RangedGetFn makeI2pRangedGet(bazarish::i2p::Router& router, std::string host,
    std::string path, const bazarish::i2p::Privacy privacy)
{
    return [router = &router, host = std::move(host),
               path = std::move(path), privacy](const std::uint64_t offset) -> RangedGet {
        std::map<std::string, std::string> headers;
        if (offset > 0) {
            headers["Range"] = "bytes=" + std::to_string(offset) + "-";
        }
        const I2pHttpResponse resp
            = i2pRequest(*router, host, "GET", path, headers, {}, privacy);
        RangedGet ranged;
        ranged.status = resp.status;
        ranged.body = resp.body;
        if (resp.status == 200) {
            ranged.total = headerUint64(resp.headers, "content-length", resp.body.size());
        } else if (resp.status == 206) {
            ranged.total = contentRangeTotal(resp.headers);
        }
        return ranged;
    };
}

// Streams a blob download straight into `sink`, resuming a dropped I2P stream
// from the current offset, and reports byte progress as it goes. Mirrors
// runResume but reads the body off the stream in chunks (so the UI sees real
// bytes received / total) instead of buffering each attempt whole in memory.
void streamResumeToSink(bazarish::i2p::Router& router, const std::string& host,
    const std::string& path, const bazarish::i2p::Privacy privacy, FileSink& sink,
    const UploadProgressFn& onProgress)
{
    constexpr int kMaxStalledAttempts = 5;
    bool haveTotal = false;
    std::uint64_t total = 0;
    int stalled = 0;
    while (!haveTotal || sink.size() < total) {
        const std::uint64_t before = sink.size();
        try {
            // A fresh throwaway destination per attempt (unlinkability).
            auto endpoint = router.createEndpoint(bazarish::i2p::EndpointConfig{
                bazarish::i2p::Keys::generate(), bazarish::i2p::LeaseSetKind::eEncrypted, privacy,
                bazarish::i2p::kDefaultTunnelQuantity, false});
            auto stream = endpoint->connect(host, std::chrono::seconds(60));
            if (!stream) {
                throw std::runtime_error("i2p blob request: cannot reach " + host);
            }
            std::map<std::string, std::string> headers;
            if (before > 0) {
                headers["Range"] = "bytes=" + std::to_string(before) + "-";
            }
            const std::string request = buildI2pHttpRequest("GET", host, path, headers, 0);
            stream->writeAll(request.data(), request.size());

            I2pHttpHead head = readI2pHttpHead(*stream);
            const Bytes leftover(head.leftover.begin(), head.leftover.end());
            if (head.status == 200) {
                // The store ignored the Range and served the whole object: rewrite.
                total = headerUint64(head.headers, "content-length", 0);
                haveTotal = total > 0;
                sink.replace(leftover);
            } else if (head.status == 206) {
                total = contentRangeTotal(head.headers);
                if (total == 0) {
                    throw std::runtime_error("blob store returned 206 without a total length");
                }
                haveTotal = true;
                sink.append(leftover);
            } else {
                throw std::runtime_error(
                    "blob download failed: status " + std::to_string(head.status));
            }
            if (onProgress && total > 0) {
                onProgress(sink.size(), total);
            }
            std::array<char, 65536> buffer{};
            for (;;) {
                const std::size_t got = stream->readSome(buffer.data(), buffer.size());
                if (got == 0) {
                    break;
                }
                sink.append(Bytes(buffer.data(), buffer.data() + got));
                if (onProgress && total > 0) {
                    onProgress(sink.size(), total);
                }
            }
            // A 200 without a Content-Length: the bytes just drained to EOF are the
            // whole object, so adopt that as the total (else the loop never ends).
            if (!haveTotal) {
                total = sink.size();
                haveTotal = true;
                if (onProgress && total > 0) {
                    onProgress(sink.size(), total);
                }
            }
        } catch (const std::exception&) {
            if (++stalled >= kMaxStalledAttempts) {
                throw;
            }
            continue;
        }
        if (sink.size() <= before) {
            if (++stalled >= kMaxStalledAttempts) {
                throw std::runtime_error("blob download stalled with no progress");
            }
        } else {
            stalled = 0;
        }
    }
}

}  // namespace

void splitBlobUrl(const std::string& blobUrl, std::string& host, std::string& path)
{
    std::string rest = blobUrl;
    if (const std::size_t scheme = rest.find("://"); scheme != std::string::npos) {
        rest = rest.substr(scheme + 3);
    }
    const std::size_t slash = rest.find('/');
    if (slash == std::string::npos) {
        throw std::runtime_error("blob url has no path");
    }
    host = rest.substr(0, slash);
    path = rest.substr(slash);
    if (host.size() < std::string(kB32Suffix).size()
        || host.compare(host.size() - std::string(kB32Suffix).size(),
               std::string(kB32Suffix).size(), kB32Suffix)
            != 0) {
        throw std::runtime_error("blob host is not a .b32.i2p address");
    }
}

I2pHttpResponse i2pRequest(bazarish::i2p::Router& router,
    const std::string& b33Host, const std::string& method, const std::string& path,
    const std::map<std::string, std::string>& headers, const Bytes& body,
    const bazarish::i2p::Privacy privacy)
{
    // A fresh throwaway destination per call (unlinkability); connecting out does
    // not need a published leaseset.
    auto endpoint = router.createEndpoint(bazarish::i2p::EndpointConfig{
        bazarish::i2p::Keys::generate(), bazarish::i2p::LeaseSetKind::eEncrypted, privacy,
        bazarish::i2p::kDefaultTunnelQuantity, false});
    auto stream = endpoint->connect(b33Host, std::chrono::seconds(60));
    if (!stream) {
        throw std::runtime_error("i2p blob request: cannot reach " + b33Host);
    }

    const std::string request = buildI2pHttpRequest(method, b33Host, path, headers, body.size());
    stream->writeAll(request.data(), request.size());
    if (!body.empty()) {
        stream->writeAll(body.data(), body.size());
    }
    // The request carries Content-Length and the server closes after responding
    // (Connection: close); reading to EOF yields the whole response. The status +
    // lowercased header map let the resume driver read Content-Length /
    // Content-Range.
    // Common's HTTP-over-stream reader; auto avoids clashing with this layer's own
    // I2pHttpResponse type (Bytes body) declared just below.
    const auto parsed = readI2pHttpResponse(*stream);
    I2pHttpResponse response;
    response.status = parsed.status;
    response.headers = parsed.headers;
    response.body = Bytes(parsed.body.begin(), parsed.body.end());
    return response;
}

Bytes downloadWithResume(const RangedGetFn& get)
{
    Bytes cipher;
    runResume(
        get, [&cipher]() { return static_cast<std::uint64_t>(cipher.size()); },
        [&cipher](Bytes&& body) { cipher = std::move(body); },
        [&cipher](const Bytes& body) { cipher.insert(cipher.end(), body.begin(), body.end()); });
    return cipher;
}

Bytes fetchBlob(bazarish::i2p::Router& router, const BlobPointer& pointer,
    const bazarish::i2p::Privacy privacy)
{
    std::string host;
    std::string path;
    splitBlobUrl(pointer.blobUrl, host, path);

    const Bytes ciphertext = downloadWithResume(makeI2pRangedGet(router, host, path, privacy));
    Bytes blob = unpackLargeBlob(ciphertext, pointer.fileKey, pointer.sha256);

    // Confirm receipt (anonymous, blobId only) so the store can reclaim it. Best
    // effort: the blob is already in hand; a failed confirm just leaves TTL to it.
    try {
        (void)i2pRequest(router, host, "POST", path + "/confirm", {}, {}, privacy);
    } catch (const std::exception&) {
        // ignore - reclamation falls back to TTL
    }
    return blob;
}

// The buffered file assembler (whole-body ranged GETs): assembles the ciphertext
// from a RangedGetFn, verifies its digest and decrypts it. Retained so the resume
// / verify / decrypt logic stays unit-testable without a live I2P transport; the
// production download path (fetchBlobToFile) streams the body for byte progress.
void assembleBlobToFile(
    const RangedGetFn& get, const BlobPointer& pointer, const std::filesystem::path& destPath)
{
    const std::filesystem::path tempPath = destPath.string() + ".part";
    try {
        FileSink sink(tempPath);
        runResume(
            get, [&sink]() { return sink.size(); },
            [&sink](Bytes&& body) { sink.replace(body); },
            [&sink](const Bytes& body) { sink.append(body); });
        sink.finish();
        if (toHex(sha256File(tempPath)) != pointer.sha256) {
            throw std::runtime_error("blob digest mismatch");
        }
        cms::unsealWithPasswordToFile(tempPath, destPath, pointer.fileKey);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        throw;
    }
    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
}

void fetchBlobToFile(bazarish::i2p::Router& router, const BlobPointer& pointer,
    const std::filesystem::path& destPath, const bazarish::i2p::Privacy privacy,
    const UploadProgressFn& onProgress)
{
    std::string host;
    std::string path;
    splitBlobUrl(pointer.blobUrl, host, path);

    // Stream the ciphertext to a temp file (reporting real byte progress), verify
    // its digest, then decrypt it file-to-file into destPath. Neither the
    // ciphertext nor the cleartext is ever held whole in memory; the temp file is
    // removed on every exit path.
    const std::filesystem::path tempPath = destPath.string() + ".part";
    try {
        FileSink sink(tempPath);
        streamResumeToSink(router, host, path, privacy, sink, onProgress);
        sink.finish();

        // Verify-then-decrypt: a tampered or truncated transfer is rejected before
        // any cleartext is produced.
        if (toHex(sha256File(tempPath)) != pointer.sha256) {
            throw std::runtime_error("blob digest mismatch");
        }
        cms::unsealWithPasswordToFile(tempPath, destPath, pointer.fileKey);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        throw;
    }
    std::error_code ec;
    std::filesystem::remove(tempPath, ec);

    // Confirm receipt (anonymous, blobId only) so the store can reclaim it. Best
    // effort: the blob is already on disk; a failed confirm just leaves TTL to it.
    try {
        (void)i2pRequest(router, host, "POST", path + "/confirm", {}, {}, privacy);
    } catch (const std::exception&) {
        // ignore - reclamation falls back to TTL
    }
}

Bytes fetchBlobViaProxy(ApiClient& api, const BlobPointer& pointer)
{
    std::string host;
    std::string path;
    splitBlobUrl(pointer.blobUrl, host, path);

    // Resume through the own server: each attempt asks it for the remaining byte
    // range, which it streams back (X-Blob-Total carries the full ciphertext
    // length, so a transfer the I2P leg truncated can be continued). The first
    // attempt is full-body (200 semantics); a resumed one continues (206 - appends
    // from the offset). The same driver as the direct path, over the proxy.
    const RangedGetFn get = [&](const std::uint64_t offset) -> RangedGet {
        std::string query = "host=" + host + "&path=" + path;
        if (offset > 0) {
            query += "&range=bytes=" + std::to_string(offset) + "-";
        }
        const ApiResponse response = api.get("/v1/messaging/blob-proxy", query);
        RangedGet ranged;
        ranged.status = (offset > 0) ? 206 : 200;
        ranged.total = headerUint64(response.headers, "x-blob-total", response.body.size());
        ranged.body = response.body;
        return ranged;
    };
    const Bytes ciphertext = downloadWithResume(get);
    return unpackLargeBlob(ciphertext, pointer.fileKey, pointer.sha256);
}

void deleteBlob(bazarish::i2p::Router& router, const std::string& blobUrl,
    const std::string& deleteToken, const bazarish::i2p::Privacy privacy)
{
    std::string host;
    std::string path;
    splitBlobUrl(blobUrl, host, path);
    (void)i2pRequest(
        router, host, "DELETE", path, {{"X-Delete-Token", deleteToken}}, {}, privacy);
}

void deleteBlobViaProxy(ApiClient& api, const std::string& blobUrl, const std::string& deleteToken)
{
    std::string host;
    std::string path;
    splitBlobUrl(blobUrl, host, path);
    (void)api.get("/v1/messaging/blob-proxy",
        "method=DELETE&host=" + host + "&path=" + path + "&token=" + deleteToken);
}

BlobUploadResult uploadBlob(
    ApiClient& api, const PackedBlob& packed, const BlobRetention& retention)
{
    std::map<std::string, std::string> headers;
    headers["X-Blob-Sha256"] = packed.sha256;
    if (retention.ttlSeconds > 0) {
        headers["X-Blob-Ttl"] = std::to_string(retention.ttlSeconds);
    }
    if (retention.count.has_value()) {
        headers["X-Blob-Count"] = std::to_string(retention.count.value());
    }

    const ApiResponse response
        = api.putBytes("/v1/storage/blob", packed.ciphertext, "application/octet-stream", headers);
    const nlohmann::json json = response.json();

    BlobUploadResult result;
    result.blobUrl = json.at("blobUrl").get<std::string>();
    result.blobId = json.at("blobId").get<std::string>();
    result.deleteToken = json.at("deleteToken").get<std::string>();
    return result;
}

BlobUploadResult uploadBlobFromFile(ApiClient& api, const PackedBlobFile& packed,
    const BlobRetention& retention, const UploadProgressFn& onProgress)
{
    std::map<std::string, std::string> headers;
    headers["X-Blob-Sha256"] = packed.sha256;
    if (retention.ttlSeconds > 0) {
        headers["X-Blob-Ttl"] = std::to_string(retention.ttlSeconds);
    }
    if (retention.count.has_value()) {
        headers["X-Blob-Count"] = std::to_string(retention.count.value());
    }

    const ApiResponse response = api.putFile("/v1/storage/blob", packed.ciphertextPath,
        packed.sha256, "application/octet-stream", headers, onProgress);
    const nlohmann::json json = response.json();

    BlobUploadResult result;
    result.blobUrl = json.at("blobUrl").get<std::string>();
    result.blobId = json.at("blobId").get<std::string>();
    result.deleteToken = json.at("deleteToken").get<std::string>();
    return result;
}

}  // namespace bazarish::client
