// Bazarish project (c) 2026
#include "BlobTransport.hpp"

#include <map>
#include <string>

namespace bazarish::client {

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
