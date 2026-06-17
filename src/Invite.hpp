// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <string>
#include <vector>

namespace bazarish::client {

// The contact invite is a small descriptor (bazarish://invite?fp&srv&srv_key) —
// see common bazarish/Descriptor.hpp (encodeDescriptor/parseDescriptor). This
// header now carries only the server-configuration link.

// A server-configuration link: a server fingerprint plus its facade URLs, so a
// client can add the server with all its settings from one link / QR — no manual
// host/port/fingerprint entry. (The fingerprint is the trust anchor; everything
// the server signs is verified against it as usual.)
struct ServerLink {
    std::string serverFingerprint;
    std::vector<std::string> facadeUrls;
};

// Encodes a server link as bazarish://server/<base64url(JSON)>.
std::string encodeServerLink(const ServerLink& link);
// Decodes such a URI. Throws on a malformed URI or payload.
ServerLink decodeServerLink(const std::string& uri);

}  // namespace bazarish::client
