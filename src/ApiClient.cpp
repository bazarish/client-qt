// Bazarish project (c) 2026
#include "ApiClient.hpp"

#include <bazarish/Auth.hpp>

#include <httplib/httplib.h>

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
    const bool authenticate)
{
    // The signed canonical path is the server-visible path: no base path and
    // no query string (the facade strips the base path before forwarding and
    // the server verifies the query-less path).
    httplib::Headers headers;
    if (authenticate) {
        const auth::Headers signedHeaders
            = auth::signRequest(identity_, nowSeconds(), method, path, body);
        headers = httplib::Headers(signedHeaders.begin(), signedHeaders.end());
        headers.emplace("X-Bazarish-Client", clientId_);
    }

    std::string url = endpoint_.basePath + path;
    if (!query.empty()) {
        url += "?" + query;
    }

    httplib::Client http(endpoint_.host, endpoint_.port);
    http.set_keep_alive(false);
    // A send may relay over I2P synchronously on the server side, which can
    // take tens of seconds (tunnel build, leaseset lookup); allow for it.
    http.set_connection_timeout(30, 0);
    http.set_read_timeout(240, 0);
    http.set_write_timeout(240, 0);

    httplib::Result result;
    if (method == "GET") {
        result = http.Get(url, headers);
    } else if (method == "POST") {
        result = http.Post(url, headers, std::string(body.begin(), body.end()), contentType);
    } else if (method == "DELETE") {
        result = http.Delete(url, headers, std::string(body.begin(), body.end()), contentType);
    } else {
        throw ApiError(std::nullopt, 0, "unsupported HTTP method: " + method);
    }

    if (!result) {
        throw ApiError(std::nullopt, 0,
            "transport failure: " + httplib::to_string(result.error()));
    }

    ApiResponse response;
    response.status = result->status;
    response.body = Bytes(result->body.begin(), result->body.end());
    response.contentType = result->get_header_value("Content-Type");

    if (response.status < 200 || response.status >= 300) {
        raiseFromResponse(response.status, response.body);
    }
    return response;
}

}  // namespace bazarish::client
