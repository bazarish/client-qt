// Bazarish project (c) 2026
#pragma once

#include "ApiClient.hpp"
#include "LargeBlob.hpp"

#include <bazarish/Bytes.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace bazarish::client {

// Retention requested at upload; mirrors blob storage's policy. Only the sender
// sets a download count; with none, reclamation is by TTL alone.
struct BlobRetention {
    std::int64_t ttlSeconds = 0;         // 0 → the store's default TTL
    std::optional<std::uint32_t> count;  // sender-set download count (none → TTL only)
};

// The capability fields blob storage returns on upload.
struct BlobUploadResult {
    std::string blobUrl;     // <b33>.b32.i2p capability URL
    std::string blobId;
    std::string deleteToken;  // kept by the client for a later unsend
};

// Uploads a packed blob's ciphertext to blob storage through the user's own
// server facade (PUT /v1/storage/blob, authenticated; the facade forwards it to
// the blob backend). Retention rides in headers (not signature-covered —
// end-to-end integrity is the sealed pointer's sha256). Throws (ApiError) on a
// non-success response, e.g. quota exceeded.
BlobUploadResult uploadBlob(
    ApiClient& api, const PackedBlob& packed, const BlobRetention& retention);

// --- Download / confirm: over I2P only (never clearnet — no IP leak) ---

struct I2pHttpResponse {
    int status = 0;
    Bytes body;
};

// Performs one HTTP/1.1 request to a .b32.i2p host over a FRESH transient SAM
// session (a throwaway destination per call, so the store cannot link a
// fetcher's requests to each other or to a messaging identity). One connection
// per call (Connection: close). Used for blob download (GET /b/<id>), confirm
// (POST /b/<id>/confirm) and unsend (DELETE /b/<id>). Construction blocks on
// tunnel build, so each call carries I2P latency. Throws on transport failure.
I2pHttpResponse i2pRequest(const std::string& samHost, std::uint16_t samPort,
    const std::string& b33Host, const std::string& method, const std::string& path,
    const std::map<std::string, std::string>& headers = {}, const Bytes& body = {});

// Downloads the ciphertext named by a pointer over I2P, verifies its digest and
// decrypts it, then confirms receipt (anonymous, blobId only). Returns the
// original message blob. Throws on a fetch error or an integrity failure.
Bytes fetchBlob(const std::string& samHost, std::uint16_t samPort, const BlobPointer& pointer);

// Splits a blob download URL ("http://<b33>.b32.i2p/b/<id>") into its host and
// path. Throws if it is not a well-formed .b32.i2p URL.
void splitBlobUrl(const std::string& blobUrl, std::string& host, std::string& path);

}  // namespace bazarish::client
