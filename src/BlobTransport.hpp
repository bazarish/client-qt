// Bazarish project (c) 2026
#pragma once

#include "ApiClient.hpp"
#include "LargeBlob.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/I2p.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace bazarish::client {

// Stage of a streaming blob fetch, surfaced for UI feedback during a long or
// flaky I2P transfer: eConnecting before the first byte, eDownloading while bytes
// flow, eReconnecting while a dropped/stalled transfer is being resumed.
enum class BlobFetchStage { eConnecting, eDownloading, eReconnecting };
using BlobStageFn = std::function<void(BlobFetchStage)>;

// Thrown when the blob store answers that the object does not exist (HTTP 404)
// or is gone (410): a definitive, non-retryable outcome (the blob aged out of the
// store by TTL or download count), distinct from a transient transport failure.
struct BlobNotFoundError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Retention requested at upload; mirrors blob storage's policy. Only the sender
// sets a download count; with none, reclamation is by TTL alone.
struct BlobRetention {
    std::int64_t ttlSeconds = 0;         // 0 -> the store's default TTL
    std::optional<std::uint32_t> count;  // sender-set download count (none -> TTL only)
};

// The capability fields blob storage returns on upload.
struct BlobUploadResult {
    std::string blobUrl;     // <b33>.b32.i2p capability URL
    std::string blobId;
    std::string deleteToken;  // kept by the client for a later unsend
};

// Uploads a packed blob's ciphertext to blob storage through the user's own
// server facade (PUT /v1/storage/blob, authenticated; the facade forwards it to
// the blob backend). Retention rides in headers (not signature-covered -
// end-to-end integrity is the sealed pointer's sha256). Throws (ApiError) on a
// non-success response, e.g. quota exceeded.
BlobUploadResult uploadBlob(
    ApiClient& api, const PackedBlob& packed, const BlobRetention& retention);

// As uploadBlob, but streams the ciphertext from packed.ciphertextPath without
// reading it into memory (the request is signed over packed.sha256). The
// counterpart to packLargeBlobToFile for the large-file upload path.
BlobUploadResult uploadBlobFromFile(ApiClient& api, const PackedBlobFile& packed,
    const BlobRetention& retention, const UploadProgressFn& onProgress = {});

// --- Download / confirm: over I2P only (never clearnet - no IP leak) ---

struct I2pHttpResponse {
    int status = 0;
    Bytes body;
    std::map<std::string, std::string> headers;  // response header names, lowercased
};

// Performs one HTTP/1.1 request to a .b32.i2p host over a FRESH transient I2P
// destination (a throwaway destination per call, so the store cannot link a
// fetcher's requests to each other or to a messaging identity). One connection
// per call (Connection: close). Used for blob download (GET /b/<id>), confirm
// (POST /b/<id>/confirm) and unsend (DELETE /b/<id>). Construction blocks on
// tunnel build, so each call carries I2P latency. Throws on transport failure.
I2pHttpResponse i2pRequest(bazarish::i2p::Router& router,
    const std::string& b33Host, const std::string& method, const std::string& path,
    const std::map<std::string, std::string>& headers = {}, const Bytes& body = {},
    bazarish::i2p::Privacy privacy = bazarish::i2p::Privacy::eMax);

// One ranged GET attempt against the blob store, as seen by the resume driver.
// total is the full ciphertext length the store declares (Content-Length for a
// 200, the size after '/' in Content-Range for a 206); body is the bytes for
// this attempt (from the requested offset for a 206, or from zero for a 200).
struct RangedGet {
    int status = 0;
    std::uint64_t total = 0;
    Bytes body;
};

// Performs one ranged GET starting at the given byte offset. Throws on transport
// failure so the driver can count it as a stalled attempt and retry.
using RangedGetFn = std::function<RangedGet(std::uint64_t offset)>;

// Assembles the full ciphertext by driving get(), resuming from the last received
// byte after a truncated transfer (an I2P stream drop) rather than restarting.
// Bounded by consecutive no-progress attempts. Throws if it cannot make progress
// or on a non-2xx status.
Bytes downloadWithResume(const RangedGetFn& get);

// Streams the ciphertext from get() to a temporary file beside destPath,
// verifies its digest against the pointer, then decrypts it file-to-file into
// destPath - so neither the ciphertext nor the cleartext is ever held whole in
// memory. The temp file is removed on every exit path. The transport seam (get)
// is injected so the assembly/verify/decrypt logic is testable without a router.
// Throws on a fetch error or an integrity failure.
void assembleBlobToFile(
    const RangedGetFn& get, const BlobPointer& pointer, const std::filesystem::path& destPath);

// Downloads the ciphertext named by a pointer over I2P (with Range/resume),
// verifies its digest and decrypts it, then confirms receipt (anonymous, blobId
// only). Returns the original message blob. Throws on a fetch error or an
// integrity failure.
Bytes fetchBlob(bazarish::i2p::Router& router, const BlobPointer& pointer,
    bazarish::i2p::Privacy privacy = bazarish::i2p::Privacy::eMax);

// Like fetchBlob, but streams the ciphertext straight to a temporary file and
// decrypts it file-to-file into destPath, so a multi-gigabyte attachment is
// never held whole in memory on the recipient. Verifies the digest before
// decrypting (a tampered or truncated transfer is rejected) and confirms
// receipt. Throws on a fetch error or an integrity failure.
// onProgress, when set, is called with (received, total) ciphertext bytes as the
// stream arrives, so the recipient can show a real download progress bar.
// onStage, when set, reports connecting/downloading/reconnecting transitions so a
// stalled transfer reads as "reconnecting" rather than a frozen bar. cancel, when
// non-null, is polled to abort promptly (closing a parked read) on teardown.
void fetchBlobToFile(bazarish::i2p::Router& router, const BlobPointer& pointer,
    const std::filesystem::path& destPath,
    bazarish::i2p::Privacy privacy = bazarish::i2p::Privacy::eMax,
    const UploadProgressFn& onProgress = {}, const BlobStageFn& onStage = {},
    const std::atomic<bool>* cancel = nullptr);

// Fetches a blob through the user's own server (the proxy-fetch fallback for
// clients with no I2P transport of their own): the server fetches the b33 ciphertext over
// I2P and relays it back over the facade. Verifies the digest and decrypts.
// Throws (ApiError) on a fetch error. No receipt confirm (TTL reclaims).
Bytes fetchBlobViaProxy(ApiClient& api, const BlobPointer& pointer);

// Sender unsend: deletes the blob over a fresh transient I2P destination, gated
// by the delete-token. Throws on transport failure.
void deleteBlob(bazarish::i2p::Router& router, const std::string& blobUrl,
    const std::string& deleteToken, bazarish::i2p::Privacy privacy = bazarish::i2p::Privacy::eMax);

// Sender unsend through the own-server I2P proxy (fallback for a client with no
// I2P transport of its own). Throws on a proxy error.
void deleteBlobViaProxy(ApiClient& api, const std::string& blobUrl, const std::string& deleteToken);

// Splits a blob download URL ("http://<b33>.b32.i2p/b/<id>") into its host and
// path. Throws if it is not a well-formed .b32.i2p URL.
void splitBlobUrl(const std::string& blobUrl, std::string& host, std::string& path);

}  // namespace bazarish::client
