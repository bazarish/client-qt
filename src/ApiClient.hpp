// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Errors.hpp>
#include <bazarish/I2p.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace bazarish::client {

// Reports upload progress as (bytes sent so far, total bytes). Invoked from the
// thread driving the upload; called repeatedly as the body streams out.
using UploadProgressFn = std::function<void(std::uint64_t sent, std::uint64_t total)>;

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
    // Response header names lowercased (e.g. the blob proxy's "x-blob-total").
    std::map<std::string, std::string> headers;

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
    // i2pDataDir is the embedded router's data directory; it enables routing
    // facades whose host ends in ".b32.i2p" over I2P. When empty, only clearnet
    // facades are usable (i2p facades are treated as unreachable) - the CLI and
    // tests that never touch I2P leave it unset.
    ApiClient(const Identity& identity, std::string clientId, ServerEndpoint endpoint,
        std::filesystem::path i2pDataDir = {});

    // Authenticated requests. path is the server-visible path (no base path,
    // no query string); query, when non-empty, is appended to the URL only.
    // Every non-2xx response throws ApiError.
    ApiResponse get(const std::string& path, const std::string& query = "");
    ApiResponse postJson(const std::string& path, const nlohmann::json& body);
    ApiResponse postBytes(
        const std::string& path, const Bytes& body, const std::string& contentType);
    // Authenticated PUT with extra request headers (e.g. blob retention).
    ApiResponse putBytes(const std::string& path, const Bytes& body,
        const std::string& contentType,
        const std::map<std::string, std::string>& extraHeaders = {});
    // Authenticated PUT that streams a file as the body without reading it into
    // memory: the request is signed over the precomputed body digest
    // (bodySha256Hex) and the file is fed to the connection through a content
    // provider. Used for large blob upload.
    ApiResponse putFile(const std::string& path, const std::filesystem::path& filePath,
        const std::string& bodySha256Hex, const std::string& contentType,
        const std::map<std::string, std::string>& extraHeaders = {},
        const UploadProgressFn& onProgress = {});
    ApiResponse del(const std::string& path, const nlohmann::json& body = nlohmann::json());

    // Unauthenticated GET (alias resolution is findable by design).
    ApiResponse getPublic(const std::string& path, const std::string& query = "");

    const std::string& clientId() const;
    const ServerEndpoint& endpoint() const;
    // The facade the transport is currently using (last one that worked), as a
    // URL - for the GUI's "connected via" display.
    std::string activeFacadeUrl() const;
    // Whether that active facade is an I2P facade (host ends in ".b32.i2p") -
    // for the account list's positive "connected over I2P" marking.
    bool activeFacadeIsI2p() const;

private:
    ApiResponse send(const std::string& method, const std::string& path,
        const std::string& query, const Bytes& body, const std::string& contentType,
        bool authenticate, const std::map<std::string, std::string>& extraHeaders = {});

    // True if a facade's host ends in ".b32.i2p" (reached over the embedded I2P
    // transport rather than clearnet).
    static bool facadeIsI2p(const Facade& facade);
    // The order facades are tried in: I2P facades first (preferred), then
    // clearnet, preserving each group's configured order.
    std::vector<std::size_t> facadeOrder() const;
    // Performs one HTTP/1.1 exchange to an I2P facade over the persistent
    // outbound destination. writeBody streams the request body onto the stream
    // after the head (bodyLen must equal the bytes it writes). Returns nullopt
    // when the facade is unreachable. Throws only on a malformed response.
    std::optional<ApiResponse> i2pExchange(const Facade& facade, const std::string& method,
        const std::string& fullPath, const std::map<std::string, std::string>& headers,
        std::size_t bodyLen, const std::function<void(bazarish::i2p::Stream&)>& writeBody);

    const Identity& identity_;
    const std::string clientId_;
    const ServerEndpoint endpoint_;
    // The embedded router's data dir (empty -> no I2P transport; i2p facades are
    // then unreachable).
    const std::filesystem::path i2pDataDir_;
    // A persistent unpublished outbound destination that dials I2P facades; its
    // tunnels stay warm across requests (a fresh transient per call would rebuild
    // a destination on every poll). Created lazily on first I2P facade use.
    std::shared_ptr<bazarish::i2p::Endpoint> i2pOut_;
    // Index of the last facade that worked; the GUI "connected via" reads it.
    std::size_t activeFacade_ = 0;
    // Serializes the two network entry points (send / putFile) so the client is
    // safe to call from more than one thread: a blob download running off the main
    // worker thread may take the own-server proxy fallback, which goes through this
    // client concurrently with the worker's sync/sends. Guards the shared lazily
    // created outbound endpoint and the active-facade index.
    mutable std::mutex netMutex_;
};

}  // namespace bazarish::client
