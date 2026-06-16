// Bazarish project (c) 2026
#include "LargeBlob.hpp"

#include <bazarish/Cms.hpp>
#include <bazarish/Crypto.hpp>

#include <filesystem>
#include <stdexcept>

namespace bazarish::client {

PackedBlob packLargeBlob(const Bytes& blob)
{
    PackedBlob packed;
    // A fresh 256-bit key, carried only in the sealed pointer; blob storage sees
    // ciphertext alone.
    packed.fileKey = toHex(randomBytes(32));
    packed.ciphertext = cms::sealWithPassword(blob, packed.fileKey);
    packed.sha256 = toHex(sha256(packed.ciphertext));
    packed.size = packed.ciphertext.size();
    return packed;
}

PackedBlobFile packLargeBlobToFile(
    const std::filesystem::path& inPath, const std::filesystem::path& ciphertextPath)
{
    PackedBlobFile packed;
    // A fresh 256-bit key, carried only in the sealed pointer; blob storage sees
    // ciphertext alone. Encrypt streams file-to-file so nothing is held in RAM.
    packed.fileKey = toHex(randomBytes(32));
    cms::sealWithPasswordToFile(inPath, ciphertextPath, packed.fileKey);
    packed.ciphertextPath = ciphertextPath;
    packed.sha256 = toHex(sha256File(ciphertextPath));
    packed.size = std::filesystem::file_size(ciphertextPath);
    return packed;
}

Bytes unpackLargeBlob(
    const Bytes& ciphertext, const std::string& fileKey, const std::string& expectedSha256)
{
    if (toHex(sha256(ciphertext)) != expectedSha256) {
        throw std::runtime_error("blob digest mismatch");
    }
    return cms::unsealWithPassword(ciphertext, fileKey);
}

nlohmann::json blobPointerToJson(const BlobPointer& pointer)
{
    return {
        {"blobUrl", pointer.blobUrl},
        {"blobId", pointer.blobId},
        {"fileKey", pointer.fileKey},
        {"sha256", pointer.sha256},
        {"size", pointer.size},
        {"meta", pointer.meta},
    };
}

BlobPointer blobPointerFromJson(const nlohmann::json& json)
{
    BlobPointer pointer;
    pointer.blobUrl = json.at("blobUrl").get<std::string>();
    pointer.blobId = json.at("blobId").get<std::string>();
    pointer.fileKey = json.at("fileKey").get<std::string>();
    pointer.sha256 = json.at("sha256").get<std::string>();
    pointer.size = json.at("size").get<std::uint64_t>();
    pointer.meta = json.value("meta", nlohmann::json::object());
    return pointer;
}

}  // namespace bazarish::client
