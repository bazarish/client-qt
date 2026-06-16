// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace bazarish::client {

// Any message blob larger than this is externalized to blob storage; only a
// small sealed pointer is delivered through the mailbox.
inline constexpr std::size_t kLargeBlobThresholdBytes = 512 * 1024;

// The ciphertext to upload plus the secrets/metadata that go into the sealed
// pointer. The blob is encrypted with a fresh random key (CMS PWRI / AES-256)
// so blob storage only ever holds opaque ciphertext.
struct PackedBlob {
    Bytes ciphertext;
    std::string fileKey;  // high-entropy key; travels only in the sealed pointer
    std::string sha256;   // hex digest of the ciphertext, binding the pointer to the bytes
    std::uint64_t size = 0;
};

// Encrypts blob with a fresh random key for upload to blob storage.
PackedBlob packLargeBlob(const Bytes& blob);

// Verifies the ciphertext digest, then decrypts it with fileKey. Throws on a
// digest mismatch (tampered/corrupt transfer) or a decryption failure.
Bytes unpackLargeBlob(
    const Bytes& ciphertext, const std::string& fileKey, const std::string& expectedSha256);

// The pointer that replaces an externalized message: built by the sender,
// sealed E2E to the recipient, parsed by the recipient before fetching. The
// download host is always a .b32.i2p (encrypted-LeaseSet) address.
struct BlobPointer {
    std::string blobUrl;
    std::string blobId;
    std::string fileKey;
    std::string sha256;
    std::uint64_t size = 0;
    nlohmann::json meta;  // filename / mime / inner content type, opaque to the store
};

nlohmann::json blobPointerToJson(const BlobPointer& pointer);
BlobPointer blobPointerFromJson(const nlohmann::json& json);

}  // namespace bazarish::client
