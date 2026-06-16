// Bazarish project (c) 2026
#include "BlobTransport.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Sam.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

namespace bazarish::client {

namespace {

constexpr const char* kB32Suffix = ".b32.i2p";

// Parses the status code from an HTTP status line ("HTTP/1.1 404 Not Found").
int parseStatus(const std::string& head)
{
    const std::size_t firstSpace = head.find(' ');
    if (firstSpace == std::string::npos) {
        throw std::runtime_error("malformed i2p http status line");
    }
    return std::stoi(head.substr(firstSpace + 1, 3));
}

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

I2pHttpResponse i2pRequest(const std::string& samHost, const std::uint16_t samPort,
    const std::string& b33Host, const std::string& method, const std::string& path,
    const std::map<std::string, std::string>& headers, const Bytes& body,
    const I2pPrivacy privacy)
{
    // A fresh throwaway destination per call (unlinkability). Construction blocks
    // on tunnel build.
    SamSession session(samHost, samPort, "blobfetch-" + toHex(randomBytes(6)),
        "TRANSIENT", kEncryptedLeaseSetType, privacy);
    SamStream stream = session.connect(b33Host);

    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: " + b33Host + "\r\n";
    for (const auto& [key, value] : headers) {
        request += key + ": " + value + "\r\n";
    }
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    request += "Connection: close\r\n\r\n";
    stream.writeAll(request.data(), request.size());
    if (!body.empty()) {
        stream.writeAll(body.data(), body.size());
    }
    // No half-close: SAM propagates a SHUT_WR as a full stream teardown, so the
    // request carries Content-Length and the server closes after responding
    // (Connection: close); reading to EOF then yields the whole response.
    std::string raw;
    std::array<char, 65536> buffer{};
    for (;;) {
        const std::size_t got = stream.readSome(buffer.data(), buffer.size());
        if (got == 0) {
            break;
        }
        raw.append(buffer.data(), got);
    }

    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        throw std::runtime_error("malformed i2p http response");
    }
    I2pHttpResponse response;
    const std::string head = raw.substr(0, headerEnd);
    const std::size_t firstLineEnd = head.find("\r\n");
    response.status = parseStatus(head.substr(0, firstLineEnd));
    // Parse the header lines (after the status line) into a lowercased map so the
    // resume driver can read Content-Length / Content-Range.
    std::size_t lineStart = (firstLineEnd == std::string::npos) ? head.size() : firstLineEnd + 2;
    while (lineStart < head.size()) {
        const std::size_t lineEnd = head.find("\r\n", lineStart);
        const std::size_t stop = (lineEnd == std::string::npos) ? head.size() : lineEnd;
        const std::string line = head.substr(lineStart, stop - lineStart);
        lineStart = (lineEnd == std::string::npos) ? head.size() : lineEnd + 2;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, colon);
        std::transform(key.begin(), key.end(), key.begin(),
            [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::size_t valueStart = colon + 1;
        while (valueStart < line.size() && (line[valueStart] == ' ' || line[valueStart] == '\t')) {
            ++valueStart;
        }
        response.headers[key] = line.substr(valueStart);
    }
    const std::string payload = raw.substr(headerEnd + 4);
    response.body = Bytes(payload.begin(), payload.end());
    return response;
}

Bytes downloadWithResume(const RangedGetFn& get)
{
    // A truncated transfer (an I2P stream drop) leaves a short body with no
    // exception; the driver resumes from the byte it stopped at. Bounded by
    // consecutive attempts that make no forward progress.
    constexpr int kMaxStalledAttempts = 5;
    Bytes cipher;
    bool haveTotal = false;
    std::uint64_t total = 0;
    int stalled = 0;
    while (!haveTotal || cipher.size() < total) {
        const std::size_t before = cipher.size();
        RangedGet attempt;
        try {
            attempt = get(cipher.size());
        } catch (const std::exception&) {
            if (++stalled >= kMaxStalledAttempts) {
                throw;
            }
            continue;
        }
        if (attempt.status == 200) {
            // The store served the whole object (e.g. it ignored the Range
            // header); take it as the authoritative full ciphertext.
            cipher = std::move(attempt.body);
            total = attempt.total > 0 ? attempt.total : cipher.size();
            haveTotal = true;
        } else if (attempt.status == 206) {
            if (attempt.total == 0) {
                throw std::runtime_error("blob store returned 206 without a total length");
            }
            cipher.insert(cipher.end(), attempt.body.begin(), attempt.body.end());
            total = attempt.total;
            haveTotal = true;
        } else {
            throw std::runtime_error(
                "blob download failed: status " + std::to_string(attempt.status));
        }
        if (cipher.size() <= before) {
            if (++stalled >= kMaxStalledAttempts) {
                throw std::runtime_error("blob download stalled with no progress");
            }
        } else {
            stalled = 0;
        }
    }
    return cipher;
}

Bytes fetchBlob(const std::string& samHost, const std::uint16_t samPort,
    const BlobPointer& pointer, const I2pPrivacy privacy)
{
    std::string host;
    std::string path;
    splitBlobUrl(pointer.blobUrl, host, path);

    // Each attempt opens a fresh transient session and (after the first) asks for
    // the remaining byte range, so a dropped stream resumes instead of restarting.
    const RangedGetFn get = [&](const std::uint64_t offset) -> RangedGet {
        std::map<std::string, std::string> headers;
        if (offset > 0) {
            headers["Range"] = "bytes=" + std::to_string(offset) + "-";
        }
        const I2pHttpResponse resp
            = i2pRequest(samHost, samPort, host, "GET", path, headers, {}, privacy);
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
    const Bytes ciphertext = downloadWithResume(get);
    Bytes blob = unpackLargeBlob(ciphertext, pointer.fileKey, pointer.sha256);

    // Confirm receipt (anonymous, blobId only) so the store can reclaim it. Best
    // effort: the blob is already in hand; a failed confirm just leaves TTL to it.
    try {
        (void)i2pRequest(samHost, samPort, host, "POST", path + "/confirm", {}, {}, privacy);
    } catch (const std::exception&) {
        // ignore — reclamation falls back to TTL
    }
    return blob;
}

Bytes fetchBlobViaProxy(ApiClient& api, const BlobPointer& pointer)
{
    std::string host;
    std::string path;
    splitBlobUrl(pointer.blobUrl, host, path);
    // The server validates the host, fetches the ciphertext over I2P and relays
    // it back; get() throws on any non-success status.
    const ApiResponse response = api.get("/v1/messaging/blob-proxy", "host=" + host + "&path=" + path);
    return unpackLargeBlob(response.body, pointer.fileKey, pointer.sha256);
}

void deleteBlob(const std::string& samHost, const std::uint16_t samPort, const std::string& blobUrl,
    const std::string& deleteToken, const I2pPrivacy privacy)
{
    std::string host;
    std::string path;
    splitBlobUrl(blobUrl, host, path);
    (void)i2pRequest(
        samHost, samPort, host, "DELETE", path, {{"X-Delete-Token", deleteToken}}, {}, privacy);
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

}  // namespace bazarish::client
