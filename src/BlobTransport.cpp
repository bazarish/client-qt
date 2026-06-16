// Bazarish project (c) 2026
#include "BlobTransport.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Sam.hpp>

#include <array>
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
    response.status = parseStatus(raw.substr(0, raw.find("\r\n")));
    const std::string payload = raw.substr(headerEnd + 4);
    response.body = Bytes(payload.begin(), payload.end());
    return response;
}

Bytes fetchBlob(const std::string& samHost, const std::uint16_t samPort,
    const BlobPointer& pointer, const I2pPrivacy privacy)
{
    std::string host;
    std::string path;
    splitBlobUrl(pointer.blobUrl, host, path);

    const I2pHttpResponse download = i2pRequest(samHost, samPort, host, "GET", path, {}, {}, privacy);
    if (download.status != 200) {
        throw std::runtime_error("blob download failed: status " + std::to_string(download.status));
    }
    Bytes blob = unpackLargeBlob(download.body, pointer.fileKey, pointer.sha256);

    // Confirm receipt (anonymous, blobId only) so the store can reclaim it. Best
    // effort: the blob is already in hand; a failed confirm just leaves TTL to it.
    try {
        (void)i2pRequest(samHost, samPort, host, "POST", path + "/confirm", {}, {}, privacy);
    } catch (const std::exception&) {
        // ignore — reclamation falls back to TTL
    }
    return blob;
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
