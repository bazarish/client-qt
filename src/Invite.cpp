// Bazarish project (c) 2026
#include "Invite.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace bazarish::client {

namespace {

constexpr const char* kInvitePrefix = "bazarish://invite/";
constexpr int kInviteFormatVersion = 1;

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

std::string encodeInvite(const Invite& invite)
{
    const nlohmann::json payload = {
        {"v", kInviteFormatVersion},
        {"sub", toBase64(invite.subscriptionCertDer)},
        {"card", toBase64(invite.serverCardDer)},
    };
    const std::string json = payload.dump();
    return std::string(kInvitePrefix) + toBase64Url(Bytes(json.begin(), json.end()));
}

Invite decodeInvite(const std::string& uri)
{
    const std::string prefix = kInvitePrefix;
    if (uri.compare(0, prefix.size(), prefix) != 0) {
        throw std::runtime_error("not a bazarish invite URI");
    }
    const Bytes jsonBytes = fromBase64Url(uri.substr(prefix.size()));
    const nlohmann::json payload = nlohmann::json::parse(jsonBytes.begin(), jsonBytes.end());
    if (payload.at("v").get<int>() != kInviteFormatVersion) {
        throw std::runtime_error("unsupported invite format version");
    }
    Invite invite;
    invite.subscriptionCertDer = fromBase64(payload.at("sub").get<std::string>());
    invite.serverCardDer = fromBase64(payload.at("card").get<std::string>());
    return invite;
}

}  // namespace bazarish::client
