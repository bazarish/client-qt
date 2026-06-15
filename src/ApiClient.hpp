// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Errors.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace bazarish::client {

// One facade entry point, parsed from a single URL. A server may expose several
// facades; the client tries them in order and fails over (see ServerEndpoint).
struct Facade {
    bool tls = false;          // https vs http
    std::string host;
    int port = 0;              // defaults to 443 (https) / 80 (http) when omitted
    // Secret URI prefix the facade strips, e.g. "/s/9f3c". Empty when the
    // reverse proxy owns the secret.
    std::string basePath;
};

// Parses "http[s]://host[:port][/base/path]" into a Facade. Throws on a malformed
// URL. A bare "host:port" with no scheme is treated as http.
Facade parseFacadeUrl(const std::string& url);
// Formats a Facade back into its canonical URL string.
std::string facadeToUrl(const Facade& facade);

// Where the client reaches the infrastructure: the serving server's fingerprint
// (the trust anchor) and an ordered list of facades the transport tries and
// fails over across. A facade's secret base path is prepended to every request
// URL but excluded from the signed canonical path (the facade strips it before
// forwarding, and the server verifies the stripped path).
struct ServerEndpoint {
    // Fingerprint of the server root key (from the registration info). Used to
    // name subscription certificates and as the local mailbox server.
    std::string serverFingerprint;
    // The ordered facades; empty means "not connected to a server yet".
    std::vector<Facade> facades;
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
    // The facade the transport is currently using (last one that worked), as a
    // URL — for the GUI's "connected via" display.
    std::string activeFacadeUrl() const;

private:
    ApiResponse send(const std::string& method, const std::string& path,
        const std::string& query, const Bytes& body, const std::string& contentType,
        bool authenticate);

    const Identity& identity_;
    const std::string clientId_;
    const ServerEndpoint endpoint_;
    // Index into facadeList() of the last facade that worked; failover starts here.
    std::size_t activeFacade_ = 0;
};

}  // namespace bazarish::client
