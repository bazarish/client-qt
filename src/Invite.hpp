// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <string>

namespace bazarish::client {

// A self-verifying contact invite: the user's subscription certificate
// (serving server + sealing prekey, signed by the user) and the serving
// server's card (transport endpoints + sealing key, signed by the server).
// Together they let a contact verify and reach the user with no trust in any
// server — every field is covered by a signature the recipient checks.
struct Invite {
    Bytes subscriptionCertDer;
    Bytes serverCardDer;
};

// Encodes an invite as a bazarish://invite/<base64url(JSON)> URI.
std::string encodeInvite(const Invite& invite);

// Decodes such a URI. Throws on a malformed URI or payload. Does NOT verify
// the certificates: the caller verifies them against the expected identity.
Invite decodeInvite(const std::string& uri);

}  // namespace bazarish::client
