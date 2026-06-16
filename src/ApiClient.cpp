// Bazarish project (c) 2026
#include "ApiClient.hpp"

#include <bazarish/Auth.hpp>

#include <httplib/httplib.h>

#include <algorithm>
#include <cctype>
#include <ctime>

namespace bazarish::client {

namespace {

std::int64_t nowSeconds()
{
    return static_cast<std::int64_t>(std::time(nullptr));
}

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

ApiClient::ApiClient(const Identity& identity, std::string clientId, ServerEndpoint endpoint)
    : identity_(identity)
    , clientId_(std::move(clientId))
    , endpoint_(std::move(endpoint))
{
}

const std::string& ApiClient::clientId() const
{
    return clientId_;
}

const ServerEndpoint& ApiClient::endpoint() const
{
    return endpoint_;
}

std::string ApiClient::activeFacadeUrl() const
{
    if (endpoint_.facades.empty()) {
        return {};
    }
    const std::size_t index = activeFacade_ < endpoint_.facades.size() ? activeFacade_ : 0;
    return facadeToUrl(endpoint_.facades[index]);
}

ApiResponse ApiClient::get(const std::string& path, const std::string& query)
{
    return send("GET", path, query, {}, {}, true);
}

ApiResponse ApiClient::postJson(const std::string& path, const nlohmann::json& body)
{
    const std::string text = body.dump();
    return send("POST", path, {}, Bytes(text.begin(), text.end()), "application/json", true);
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

ApiResponse ApiClient::send(const std::string& method, const std::string& path,
    const std::string& query, const Bytes& body, const std::string& contentType,
    const bool authenticate, const std::map<std::string, std::string>& extraHeaders)
{
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
    // not integrity-critical — end-to-end integrity is the sealed pointer's
    // sha256).
    for (const auto& [key, value] : extraHeaders) {
        headers.emplace(key, value);
    }

    // Issues the request against one facade. A send may relay over I2P
    // synchronously on the server side (tens of seconds), so the timeouts are
    // generous. Returns the result; an empty result means the facade was
    // unreachable.
    const auto attempt = [&](const Facade& facade) -> httplib::Result {
        std::string url = facade.basePath + path;
        if (!query.empty()) {
            url += "?" + query;
        }
        const auto run = [&](auto& http) -> httplib::Result {
            http.set_keep_alive(false);
            http.set_connection_timeout(30, 0);
            http.set_read_timeout(240, 0);
            http.set_write_timeout(240, 0);
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

    // Try facades in order, starting from the last that worked, and cycle once
    // through all of them. A reachable facade that returns an error response is
    // final (no failover); only an unreachable facade advances to the next. The
    // caller's retry loop re-enters here, so failover continues without end.
    const std::vector<Facade>& facades = endpoint_.facades;
    std::string lastError = "no facade configured";
    for (std::size_t i = 0; i < facades.size(); ++i) {
        const std::size_t index = (activeFacade_ + i) % facades.size();
        const httplib::Result result = attempt(facades[index]);
        if (!result) {
            lastError = "transport failure: " + httplib::to_string(result.error());
            continue;  // facade unreachable — try the next
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
    throw ApiError(std::nullopt, 0, "all facades unreachable: " + lastError);
}

}  // namespace bazarish::client
