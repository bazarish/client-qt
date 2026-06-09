// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Errors.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <stdexcept>
#include <string>

namespace bazarish::client {

// Where the client reaches the infrastructure: a single facade entry point.
// The secret base path is part of the registration info and is prepended to
// every request URL but excluded from the signed canonical path (the facade
// strips it before forwarding, and the server verifies the stripped path).
struct ServerEndpoint {
    std::string host = "127.0.0.1";
    int port = 0;
    // Secret URI prefix the facade strips, e.g. "/s/9f3c". Empty when the
    // reverse proxy owns the secret.
    std::string basePath;
    // Fingerprint of the server root key (from the registration info). Used
    // to name subscription certificates and as the local mailbox server.
    std::string serverFingerprint;
};

// A server response. Non-2xx statuses are turned into ApiError by ApiClient,
// so callers only ever see successful responses here.
struct ApiResponse {
    int status = 0;
    Bytes body;
    std::string contentType;

    nlohmann::json json() const;
};

// Raised for every non-success outcome: typed server error envelopes and
// transport-level failures alike. code is set only when the body carried a
// recognized error envelope; transport failures and unrecognized bodies
// leave it empty.
class ApiError : public std::runtime_error {
public:
    ApiError(std::optional<ErrorCode> code, int httpStatus, const std::string& message);

    std::optional<ErrorCode> code;
    // 0 when there was no HTTP response at all (transport failure).
    int httpStatus = 0;
};

// Low-level signed HTTP transport for the client API. Holds a reference to
// the caller's identity (which must outlive the transport) and signs every
// authenticated request with both identity keys.
class ApiClient {
public:
    ApiClient(const Identity& identity, std::string clientId, ServerEndpoint endpoint);

    // Authenticated requests. path is the server-visible path (no base path,
    // no query string); query, when non-empty, is appended to the URL only.
    // Every non-2xx response throws ApiError.
    ApiResponse get(const std::string& path, const std::string& query = "");
    ApiResponse postJson(const std::string& path, const nlohmann::json& body);
    ApiResponse postBytes(
        const std::string& path, const Bytes& body, const std::string& contentType);
    ApiResponse del(const std::string& path, const nlohmann::json& body = nlohmann::json());

    // Unauthenticated GET (alias resolution is findable by design).
    ApiResponse getPublic(const std::string& path, const std::string& query = "");

    const std::string& clientId() const;
    const ServerEndpoint& endpoint() const;

private:
    ApiResponse send(const std::string& method, const std::string& path,
        const std::string& query, const Bytes& body, const std::string& contentType,
        bool authenticate);

    const Identity& identity_;
    const std::string clientId_;
    const ServerEndpoint endpoint_;
};

}  // namespace bazarish::client
