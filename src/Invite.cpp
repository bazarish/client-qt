// Bazarish project (c) 2026
#include "Invite.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace bazarish::client {

namespace {

constexpr const char* kServerPrefix = "bazarish://server/";
constexpr int kServerLinkFormatVersion = 1;

// base64url (RFC 4648 §5) is base64 with a URL-safe alphabet and no padding,
// so the blob can ride inside a URI path with no percent-encoding.
std::string toBase64Url(const Bytes& data)
{
    std::string text = toBase64(data);
    for (char& c : text) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        }
    }
    while (!text.empty() && text.back() == '=') {
        text.pop_back();
    }
    return text;
}

Bytes fromBase64Url(const std::string& text)
{
    std::string standard = text;
    for (char& c : standard) {
        if (c == '-') {
            c = '+';
        } else if (c == '_') {
            c = '/';
        }
    }
    while (standard.size() % 4 != 0) {
        standard.push_back('=');
    }
    return fromBase64(standard);
}

}  // namespace

std::string encodeServerLink(const ServerLink& link)
{
    const nlohmann::json payload = {
        {"v", kServerLinkFormatVersion},
        {"fp", link.serverFingerprint},
        {"facades", link.facadeUrls},
    };
    const std::string json = payload.dump();
    return std::string(kServerPrefix) + toBase64Url(Bytes(json.begin(), json.end()));
}

ServerLink decodeServerLink(const std::string& uri)
{
    const std::string prefix = kServerPrefix;
    if (uri.compare(0, prefix.size(), prefix) != 0) {
        throw std::runtime_error("not a bazarish server URI");
    }
    const Bytes jsonBytes = fromBase64Url(uri.substr(prefix.size()));
    const nlohmann::json payload = nlohmann::json::parse(jsonBytes.begin(), jsonBytes.end());
    if (payload.at("v").get<int>() != kServerLinkFormatVersion) {
        throw std::runtime_error("unsupported server link format version");
    }
    ServerLink link;
    link.serverFingerprint = payload.at("fp").get<std::string>();
    link.facadeUrls = payload.at("facades").get<std::vector<std::string>>();
    return link;
}

}  // namespace bazarish::client
