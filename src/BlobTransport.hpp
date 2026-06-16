// Bazarish project (c) 2026
#pragma once

#include "ApiClient.hpp"
#include "LargeBlob.hpp"

#include <cstdint>
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

}  // namespace bazarish::client
