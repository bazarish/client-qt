// Bazarish project (c) 2026
#include "ApiClient.hpp"

#include "I2pRouter.hpp"

#include <bazarish/Auth.hpp>
#include <bazarish/Log.hpp>
#include <bazarish/I2pHttp.hpp>

#include <httplib/httplib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>

namespace bazarish::client {

namespace {

std::int64_t nowSeconds()
{
    return static_cast<std::int64_t>(std::time(nullptr));
}

// How long to wait for our own outbound destination's tunnels on a cold start,
// and how long a dial to a facade may take once they are up.
constexpr int kOutboundReadySeconds = 180;
constexpr int kFacadeDialSeconds = 60;

// Turns a non-2xx response into a typed ApiError. A recognized error
// envelope yields its code and message; anything else keeps the raw body.
[[noreturn]] void raiseFromResponse(const int status, const Bytes& body)
{
    const std::string text(body.begin(), body.end());
    try {
        const nlohmann::json document = nlohmann::json::parse(text);
        const std::optional<ParsedError> parsed = parseErrorEnvelope(document);
        if (parsed.has_value()) {
            throw ApiError(parsed->code, status, parsed->message);
        }
    } catch (const nlohmann::json::exception&) {
        // Body was not JSON; fall through to the generic error below.
    }
    throw ApiError(std::nullopt, status, text);
}

}  // namespace

Facade parseFacadeUrl(const std::string& url)
{
    Facade facade;
    std::string rest = url;

    const std::string::size_type schemeEnd = rest.find("://");
    if (schemeEnd != std::string::npos) {
        const std::string scheme = rest.substr(0, schemeEnd);
        if (scheme == "https") {
            facade.tls = true;
        } else if (scheme != "http") {
            throw std::runtime_error("unsupported facade scheme: " + scheme);
        }
        rest = rest.substr(schemeEnd + 3);
    }
    if (rest.empty()) {
        throw std::runtime_error("empty facade URL");
    }

    const std::string::size_type slash = rest.find('/');
    const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    facade.basePath = slash == std::string::npos ? std::string() : rest.substr(slash);
    while (facade.basePath.size() > 1 && facade.basePath.back() == '/') {
        facade.basePath.pop_back();
    }
    if (facade.basePath == "/") {
        facade.basePath.clear();
    }

    const std::string::size_type colon = authority.find(':');
    if (colon != std::string::npos) {
        facade.host = authority.substr(0, colon);
        try {
            facade.port = std::stoi(authority.substr(colon + 1));
        } catch (const std::exception&) {
            throw std::runtime_error("invalid facade port in: " + url);
        }
        if (facade.port <= 0 || facade.port > 65535) {
            throw std::runtime_error("invalid facade port in: " + url);
        }
    } else {
        facade.host = authority;
        facade.port = facade.tls ? 443 : 80;
    }
    if (facade.host.empty()) {
        throw std::runtime_error("empty facade host in: " + url);
    }
    return facade;
}

std::string facadeToUrl(const Facade& facade)
{
    std::string url = (facade.tls ? "https://" : "http://") + facade.host;
    const int defaultPort = facade.tls ? 443 : 80;
    if (facade.port != 0 && facade.port != defaultPort) {
        url += ":" + std::to_string(facade.port);
    }
    url += facade.basePath;
    return url;
}

nlohmann::json ApiResponse::json() const
{
    return nlohmann::json::parse(body.begin(), body.end());
}

ApiError::ApiError(
    const std::optional<ErrorCode> code, const int httpStatus, const std::string& message)
    : std::runtime_error(message)
    , code(code)
    , httpStatus(httpStatus)
{
}

ApiClient::ApiClient(const Identity& identity, std::string clientId, ServerEndpoint endpoint,
    std::filesystem::path i2pDataDir)
    : identity_(identity)
    , clientId_(std::move(clientId))
    , endpoint_(std::move(endpoint))
    , i2pDataDir_(std::move(i2pDataDir))
{
    // Point the "connected via" display at the preferred facade before the first
    // request confirms one (I2P is tried first, so it reads as the active one).
    if (!endpoint_.facades.empty()) {
        activeFacade_ = facadeOrder().front();
    }
}

bool ApiClient::facadeIsI2p(const Facade& facade)
{
    static const std::string kSuffix = ".b32.i2p";
    const std::string& host = facade.host;
    return host.size() >= kSuffix.size()
        && host.compare(host.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
}

std::vector<std::size_t> ApiClient::facadeOrder() const
{
    // I2P facades are preferred, but only when an I2P transport is configured;
    // without one they are unreachable, so clearnet goes first instead (and the
    // "connected via" display does not falsely claim I2P). The preferred group is
    // emitted first, each group keeping its configured order.
    const bool preferI2p = !i2pDataDir_.empty();
    std::vector<std::size_t> order;
    order.reserve(endpoint_.facades.size());
    for (std::size_t i = 0; i < endpoint_.facades.size(); ++i) {
        if (facadeIsI2p(endpoint_.facades[i]) == preferI2p) {
            order.push_back(i);
        }
    }
    for (std::size_t i = 0; i < endpoint_.facades.size(); ++i) {
        if (facadeIsI2p(endpoint_.facades[i]) != preferI2p) {
            order.push_back(i);
        }
    }
    return order;
}

const std::string& ApiClient::clientId() const
{
    return clientId_;
}

const ServerEndpoint& ApiClient::endpoint() const
{
    return endpoint_;
}

void ApiClient::markI2pProven()
{
    if (i2pProven_) {
        return;
    }
    i2pProven_ = true;
    bazarish::log::info("this profile now reaches its server over I2P; clearnet is refused");
    if (onI2pProven_) {
        onI2pProven_();
    }
}

void ApiClient::setI2pProven(const bool proven)
{
    i2pProven_ = proven;
}

bool ApiClient::i2pProven() const
{
    return i2pProven_;
}

void ApiClient::setAllowClearnet(const bool allow)
{
    allowClearnet_ = allow;
}

bool ApiClient::allowClearnet() const
{
    return allowClearnet_;
}

void ApiClient::setDestinationOwner(std::string owner)
{
    destinationOwner_ = std::move(owner);
}

void ApiClient::setOnI2pProven(std::function<void()> callback)
{
    onI2pProven_ = std::move(callback);
}

std::string ApiClient::activeFacadeUrl() const
{
    if (endpoint_.facades.empty()) {
        return {};
    }
    const std::size_t index = activeFacade_ < endpoint_.facades.size() ? activeFacade_ : 0;
    return facadeToUrl(endpoint_.facades[index]);
}

bool ApiClient::activeFacadeIsI2p() const
{
    if (endpoint_.facades.empty()) {
        return false;
    }
    const std::size_t index = activeFacade_ < endpoint_.facades.size() ? activeFacade_ : 0;
    return facadeIsI2p(endpoint_.facades[index]);
}

std::optional<ApiResponse> ApiClient::i2pExchange(const Facade& facade, const std::string& method,
    const std::string& fullPath, const std::map<std::string, std::string>& headers,
    const std::size_t bodyLen, const std::function<void(bazarish::i2p::Stream&)>& writeBody)
{
    reportConnectProgress(30, "Starting the I2P router");
    sharedI2pRouter(i2pDataDir_);  // started here if it is not up yet
    if (!i2pOut_) {
        reportConnectProgress(40, "Building your I2P tunnels");
        // One destination per profile, shared with everything else that dials its
        // facade: streams multiplex over it, so a second one would only mean a
        // second set of tunnels.
        i2pOut_ = facadeLinkFor(destinationOwner_, bazarish::i2p::Privacy::eMax);
        if (!i2pOut_) {
            return std::nullopt;  // no router yet: the caller falls back or retries
        }
    }
    // A dial from a destination whose tunnels are still building fails for a
    // reason that has nothing to do with the facade, and would be reported as an
    // unreachable one. Wait for our own side first; only then is a failure the
    // facade's.
    if (!i2pOut_->waitReady(std::chrono::seconds(kOutboundReadySeconds))) {
        reportConnectProgress(40, "I2P tunnels are still building");
        return std::nullopt;
    }
    reportConnectProgress(55, "Looking up the server's I2P address");
    std::unique_ptr<bazarish::i2p::Stream> stream
        = i2pOut_->connect(facade.host, std::chrono::seconds(kFacadeDialSeconds));
    if (!stream) {
        return std::nullopt;  // facade unreachable - try the next
    }
    reportConnectProgress(65, "Connected to the server over I2P");

    const std::string head = buildI2pHttpRequest(method, facade.host, fullPath, headers, bodyLen);
    stream->writeAll(head.data(), head.size());
    if (bodyLen > 0 && writeBody) {
        writeBody(*stream);
    }

    // The request carries Content-Length and the facade closes after responding
    // (Connection: close), so reading to EOF yields the whole response. `auto`
    // avoids clashing with bazarish::client::I2pHttpResponse declared elsewhere.
    // A framing failure here is worth naming: the request went out over I2P and
    // something came back that the reader could not frame, which reads very
    // differently from an unreachable facade.
    bazarish::i2p::Stream& responseStream = *stream;
    const auto parsed = [&responseStream, &facade]() {
        try {
            return readI2pHttpResponse(responseStream);
        } catch (const std::exception& error) {
            bazarish::log::warn("i2p facade {} answered unframed: {}",
                facade.host.substr(0, 12), error.what());
            throw;
        }
    }();
    ApiResponse response;
    response.status = parsed.status;
    response.body = Bytes(parsed.body.begin(), parsed.body.end());
    if (const auto it = parsed.headers.find("content-type"); it != parsed.headers.end()) {
        response.contentType = it->second;
    }
    response.headers = parsed.headers;  // already lowercased by the parser
    return response;
}

ApiResponse ApiClient::get(const std::string& path, const std::string& query)
{
    return send("GET", path, query, {}, {}, true);
}

ApiResponse ApiClient::getWaiting(
    const std::string& path, const std::string& query, const int readTimeoutSeconds)
{
    return send("GET", path, query, {}, {}, true, {}, readTimeoutSeconds);
}

ApiResponse ApiClient::postJson(
    const std::string& path, const nlohmann::json& body, const int readTimeoutSeconds)
{
    const std::string text = body.dump();
    return send("POST", path, {}, Bytes(text.begin(), text.end()), "application/json", true, {},
        readTimeoutSeconds);
}

ApiResponse ApiClient::postBytes(
    const std::string& path, const Bytes& body, const std::string& contentType)
{
    return send("POST", path, {}, body, contentType, true);
}

ApiResponse ApiClient::putBytes(const std::string& path, const Bytes& body,
    const std::string& contentType, const std::map<std::string, std::string>& extraHeaders)
{
    return send("PUT", path, {}, body, contentType, true, extraHeaders);
}

ApiResponse ApiClient::del(const std::string& path, const nlohmann::json& body)
{
    Bytes encoded;
    std::string contentType;
    if (!body.is_null()) {
        const std::string text = body.dump();
        encoded.assign(text.begin(), text.end());
        contentType = "application/json";
    }
    return send("DELETE", path, {}, encoded, contentType, true);
}

ApiResponse ApiClient::getPublic(const std::string& path, const std::string& query)
{
    return send("GET", path, query, {}, {}, false);
}

ApiResponse ApiClient::getClearnet(const std::string& path, const std::string& query)
{
    return send("GET", path, query, {}, {}, false, {}, kDefaultReadTimeoutSeconds, true);
}

void ApiClient::seedRouterFromServer()
{
    if (i2pDataDir_.empty() || !i2pEnabled()) {
        return;  // no embedded transport to bootstrap
    }
    const bool seeded = seedRouterOnce(i2pDataDir_, [this]() {
        reportConnectProgress(10, "Asking your server for the I2P network database");
        // Clearnet by construction: this call bootstraps the transport itself.
        const nlohmann::json body = getClearnet("/v1/messaging/reseed").json();
        std::vector<Bytes> routers;
        for (const nlohmann::json& entry : body.at("routers")) {
            routers.push_back(fromBase64(entry.get<std::string>()));
        }
        return routers;
    });
    // i2pd's own reseed hosts are the last resort, and only when there is nobody
    // to ask: no clearnet facade in this server's descriptor, or none answered.
    // Otherwise the bootstrap stays between the user and their own server.
    if (seeded) {
        reportConnectProgress(25, "Network database ready");
    }
    if (!seeded) {
        const bool haveClearnetFacade = std::any_of(endpoint_.facades.begin(),
            endpoint_.facades.end(), [](const Facade& f) { return !facadeIsI2p(f); });
        setPublicReseedAllowed(true);
        reportConnectProgress(25, "Your server did not answer: bootstrapping from public reseeds");
        if (haveClearnetFacade) {
            bazarish::log::warn(
                "no clearnet facade answered the reseed: falling back to public reseed hosts");
        } else {
            bazarish::log::warn(
                "this server publishes no clearnet facade: falling back to public reseed hosts");
        }
    }
}

ApiResponse ApiClient::send(const std::string& method, const std::string& path,
    const std::string& query, const Bytes& body, const std::string& contentType,
    const bool authenticate, const std::map<std::string, std::string>& extraHeaders,
    const int readTimeoutSeconds, const bool clearnetOnly)
{
    // Before the request lock: a call that may go over I2P must not start the
    // router unseeded. A clearnet-only call skips it - it cannot start the
    // router, and the seeding fetch is itself one of those.
    if (!clearnetOnly) {
        seedRouterFromServer();
    }
    const std::lock_guard<std::mutex> lock(netMutex_);
    // The signed canonical path is the server-visible path: no base path and
    // no query string (the facade strips the base path before forwarding and
    // the server verifies the query-less path). The signature is therefore the
    // same across facades, so it is computed once.
    httplib::Headers headers;
    if (authenticate) {
        const auth::Headers signedHeaders
            = auth::signRequest(identity_, nowSeconds(), method, path, body);
        headers = httplib::Headers(signedHeaders.begin(), signedHeaders.end());
        headers.emplace("X-Bazarish-Client", clientId_);
    }
    // Extra headers ride outside the signature (e.g. blob retention, which is
    // not integrity-critical - end-to-end integrity is the sealed pointer's
    // sha256).
    for (const auto& [key, value] : extraHeaders) {
        headers.emplace(key, value);
    }

    // The same headers as a plain map for the I2P transport (which writes them
    // verbatim; Host / Content-Length / Connection are added by the builder).
    std::map<std::string, std::string> i2pHeaders;
    for (const auto& [key, value] : headers) {
        i2pHeaders[key] = value;
    }
    if (!body.empty() && !contentType.empty()) {
        i2pHeaders["Content-Type"] = contentType;
    }

    // Issues the request against one clearnet facade. A send may relay over I2P
    // synchronously on the server side (tens of seconds), so the timeouts are
    // generous. An empty result means the facade was unreachable.
    const auto clearnetAttempt = [&](const Facade& facade) -> httplib::Result {
        std::string url = facade.basePath + path;
        if (!query.empty()) {
            url += "?" + query;
        }
        const auto run = [&](auto& http) -> httplib::Result {
            http.set_keep_alive(false);
            http.set_connection_timeout(kConnectTimeoutSeconds, 0);
            http.set_read_timeout(readTimeoutSeconds, 0);
            http.set_write_timeout(kWriteTimeoutSeconds, 0);
            if (method == "GET") {
                return http.Get(url, headers);
            }
            if (method == "POST") {
                return http.Post(url, headers, std::string(body.begin(), body.end()), contentType);
            }
            if (method == "DELETE") {
                return http.Delete(
                    url, headers, std::string(body.begin(), body.end()), contentType);
            }
            if (method == "PUT") {
                return http.Put(url, headers, std::string(body.begin(), body.end()), contentType);
            }
            throw ApiError(std::nullopt, 0, "unsupported HTTP method: " + method);
        };
        if (facade.tls) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient http(facade.host, facade.port);
            // The facade is the untrusted last mile (security is end-to-end and
            // anchored in the server fingerprint, not TLS PKI), so a self-signed
            // or proxy certificate is accepted.
            http.enable_server_certificate_verification(false);
            return run(http);
#else
            throw ApiError(std::nullopt, 0, "https facade not supported in this build");
#endif
        }
        httplib::Client http(facade.host, facade.port);
        return run(http);
    };

    // Try facades in priority order (I2P first), failing over only when a facade
    // is unreachable. A reachable facade that returns an error response is final
    // (no failover); the caller's retry loop re-enters here so failover continues.
    const std::vector<Facade>& facades = endpoint_.facades;
    std::string lastError = "no facade configured";
    for (const std::size_t index : facadeOrder()) {
        const Facade& facade = facades[index];

        if (facadeIsI2p(facade)) {
            if (clearnetOnly) {
                // The caller bootstraps the I2P transport itself; it cannot use it.
                lastError = "clearnet-only request: " + facade.host;
                continue;
            }
            if (!i2pEnabled()) {
                // I2P turned off in settings: use clearnet facades only. With no
                // reachable clearnet facade the loop ends in an explicit error.
                lastError = "i2p is turned off (clearnet only): " + facade.host;
                continue;
            }
            if (i2pDataDir_.empty()) {
                lastError = "i2p facade without an I2P transport: " + facade.host;
                continue;
            }
            std::string fullPath = facade.basePath + path;
            if (!query.empty()) {
                fullPath += "?" + query;
            }
            const std::optional<ApiResponse> response
                = i2pExchange(facade, method, fullPath, i2pHeaders, body.size(),
                    [&body](bazarish::i2p::Stream& stream) {
                        stream.writeAll(body.data(), body.size());
                    });
            if (!response) {
                lastError = "i2p facade unreachable: " + facade.host;
                continue;
            }
            activeFacade_ = index;
            markI2pProven();
            if (response->status < 200 || response->status >= 300) {
                raiseFromResponse(response->status, response->body);
            }
            return *response;
        }

        // The reseed is the one clearnet request that stays allowed: it carries no
        // identity, and it is what makes I2P possible at all - refusing it would
        // leave a fresh profile with no way to ever reach an I2P facade.
        if (!clearnetOnly) {
            if (fullPrivacy()) {
                // Full privacy mode: never touch a clearnet facade. With no reachable
                // I2P facade the loop ends in an explicit error and the profile is offline.
                lastError = "full privacy mode (I2P only): " + facade.host;
                continue;
            }
            if (i2pProven_ && !allowClearnet_) {
                // This profile has already talked over I2P, so a clearnet request
                // now would be a silent downgrade, not a first connection.
                lastError = "this profile reaches its server over I2P; clearnet refused: "
                    + facade.host;
                continue;
            }
        }
        const httplib::Result result = clearnetAttempt(facade);
        if (!result) {
            // A read timeout is not an unreachable facade: the request arrived and
            // the server is still working on it (a federated fetch dials the peer
            // over I2P, which is slow on a cold router). Say which of the two it
            // was, or the next reader goes looking at the facade for nothing.
            lastError = result.error() == httplib::Error::Read
                ? "no response within " + std::to_string(readTimeoutSeconds)
                    + "s: " + facade.host
                : "transport failure: " + httplib::to_string(result.error());
            continue;  // try the next facade
        }
        // A request that leaves over clearnet binds this account's keys to this
        // IP at the server, and shows an on-path observer which server the user
        // talks to. It is a downgrade, so say it out loud rather than let a flaky
        // I2P link move a user onto the clearnet unannounced.
        if (index != activeFacade_ && !endpoint_.facades.empty() && i2pDataDir_.empty() == false) {
            const bool i2pConfigured = std::any_of(endpoint_.facades.begin(),
                endpoint_.facades.end(), [](const Facade& f) { return facadeIsI2p(f); });
            if (i2pConfigured) {
                bazarish::log::warn(
                    "falling back to the clearnet facade {}: this request leaves I2P",
                    facade.host);
            }
        }
        activeFacade_ = index;  // remember the working facade for next time

        ApiResponse response;
        response.status = result->status;
        response.body = Bytes(result->body.begin(), result->body.end());
        response.contentType = result->get_header_value("Content-Type");
        for (const auto& [name, value] : result->headers) {
            std::string key = name;
            std::transform(key.begin(), key.end(), key.begin(),
                [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            response.headers[key] = value;
        }
        if (response.status < 200 || response.status >= 300) {
            raiseFromResponse(response.status, response.body);
        }
        return response;
    }
    throw ApiError(std::nullopt, 0, "no facade answered: " + lastError);
}

ApiResponse ApiClient::putFile(const std::string& path, const std::filesystem::path& filePath,
    const std::string& bodySha256Hex, const std::string& contentType,
    const std::map<std::string, std::string>& extraHeaders, const UploadProgressFn& onProgress)
{
    // Before the request lock: a call that may go over I2P must not start the
    // router unseeded, and the seeding is itself a (clearnet) request.
    seedRouterFromServer();
    const std::lock_guard<std::mutex> lock(netMutex_);
    const std::uintmax_t length = std::filesystem::file_size(filePath);

    // The body is signed only through its digest, so a multi-gigabyte file is
    // never materialized to sign or send it.
    const auth::Headers signedHeaders
        = auth::signRequestDigest(identity_, nowSeconds(), "PUT", path, bodySha256Hex);
    httplib::Headers headers(signedHeaders.begin(), signedHeaders.end());
    headers.emplace("X-Bazarish-Client", clientId_);
    for (const auto& [key, value] : extraHeaders) {
        headers.emplace(key, value);
    }

    std::map<std::string, std::string> i2pHeaders;
    for (const auto& [key, value] : headers) {
        i2pHeaders[key] = value;
    }
    if (!contentType.empty()) {
        i2pHeaders["Content-Type"] = contentType;
    }

    const auto clearnetAttempt = [&](const Facade& facade) -> httplib::Result {
        const std::string url = facade.basePath + path;
        // A fresh stream per attempt so a facade failover restarts cleanly from
        // the beginning of the file (the content provider seeks within it).
        const auto file = std::make_shared<std::ifstream>(filePath, std::ios::binary);
        if (!*file) {
            throw ApiError(std::nullopt, 0, "cannot open blob file: " + filePath.string());
        }
        const httplib::ContentProvider provider
            = [file, length, &onProgress](const std::size_t offset, const std::size_t want,
                  httplib::DataSink& sink) -> bool {
            file->clear();
            file->seekg(static_cast<std::streamoff>(offset));
            std::array<char, 64 * 1024> buffer;
            std::size_t remaining = want;
            std::size_t produced = offset;  // bytes of the body emitted so far
            while (remaining > 0) {
                const std::streamsize chunk = static_cast<std::streamsize>(
                    std::min<std::size_t>(remaining, buffer.size()));
                file->read(buffer.data(), chunk);
                const std::streamsize got = file->gcount();
                if (got <= 0) {
                    break;
                }
                if (!sink.write(buffer.data(), static_cast<std::size_t>(got))) {
                    return false;
                }
                remaining -= static_cast<std::size_t>(got);
                produced += static_cast<std::size_t>(got);
                if (onProgress) {
                    onProgress(produced, static_cast<std::uint64_t>(length));
                }
            }
            return true;
        };
        const auto run = [&](auto& http) -> httplib::Result {
            http.set_keep_alive(false);
            http.set_connection_timeout(kConnectTimeoutSeconds, 0);
            http.set_read_timeout(kDefaultReadTimeoutSeconds, 0);
            http.set_write_timeout(kWriteTimeoutSeconds, 0);
            return http.Put(url, headers, static_cast<std::size_t>(length), provider, contentType);
        };
        if (facade.tls) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient http(facade.host, facade.port);
            http.enable_server_certificate_verification(false);
            return run(http);
#else
            throw ApiError(std::nullopt, 0, "https facade not supported in this build");
#endif
        }
        httplib::Client http(facade.host, facade.port);
        return run(http);
    };

    // Streams the file body onto an I2P stream after the request head (the i2p
    // counterpart of the clearnet content provider).
    const auto writeFileBody = [&filePath, length](bazarish::i2p::Stream& stream) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            throw ApiError(std::nullopt, 0, "cannot open blob file: " + filePath.string());
        }
        std::array<char, 64 * 1024> buffer;
        std::uintmax_t remaining = length;
        while (remaining > 0) {
            const std::streamsize chunk = static_cast<std::streamsize>(
                std::min<std::uintmax_t>(remaining, buffer.size()));
            file.read(buffer.data(), chunk);
            const std::streamsize got = file.gcount();
            if (got <= 0) {
                break;
            }
            stream.writeAll(buffer.data(), static_cast<std::size_t>(got));
            remaining -= static_cast<std::uintmax_t>(got);
        }
    };

    const std::vector<Facade>& facades = endpoint_.facades;
    std::string lastError = "no facade configured";
    for (const std::size_t index : facadeOrder()) {
        const Facade& facade = facades[index];

        if (facadeIsI2p(facade)) {
            if (!i2pEnabled()) {
                // I2P turned off in settings: use clearnet facades only. With no
                // reachable clearnet facade the loop ends in an explicit error.
                lastError = "i2p is turned off (clearnet only): " + facade.host;
                continue;
            }
            if (i2pDataDir_.empty()) {
                lastError = "i2p facade without an I2P transport: " + facade.host;
                continue;
            }
            const std::optional<ApiResponse> response = i2pExchange(facade, "PUT",
                facade.basePath + path, i2pHeaders, static_cast<std::size_t>(length), writeFileBody);
            if (!response) {
                lastError = "i2p facade unreachable: " + facade.host;
                continue;
            }
            activeFacade_ = index;
            if (response->status < 200 || response->status >= 300) {
                raiseFromResponse(response->status, response->body);
            }
            return *response;
        }

        if (fullPrivacy()) {
            // Full privacy mode: never touch a clearnet facade (see send()).
            lastError = "full privacy mode (I2P only): " + facade.host;
            continue;
        }
        if (i2pProven_ && !allowClearnet_) {
            lastError = "this profile reaches its server over I2P; clearnet refused: "
                + facade.host;
            continue;
        }
        const httplib::Result result = clearnetAttempt(facade);
        if (!result) {
            // A read timeout is not an unreachable facade: the request arrived and
            // the server is still working on it (a federated fetch dials the peer
            // over I2P, which is slow on a cold router). Say which of the two it
            // was, or the next reader goes looking at the facade for nothing.
            lastError = result.error() == httplib::Error::Read
                ? "no response within " + std::to_string(kDefaultReadTimeoutSeconds)
                    + "s: " + facade.host
                : "transport failure: " + httplib::to_string(result.error());
            continue;  // try the next facade
        }
        activeFacade_ = index;

        ApiResponse response;
        response.status = result->status;
        response.body = Bytes(result->body.begin(), result->body.end());
        response.contentType = result->get_header_value("Content-Type");
        for (const auto& [name, value] : result->headers) {
            std::string key = name;
            std::transform(key.begin(), key.end(), key.begin(),
                [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            response.headers[key] = value;
        }
        if (response.status < 200 || response.status >= 300) {
            raiseFromResponse(response.status, response.body);
        }
        return response;
    }
    throw ApiError(std::nullopt, 0, "no facade answered: " + lastError);
}

}  // namespace bazarish::client
