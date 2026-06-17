// Bazarish project (c) 2026
#include "I2pKeys.hpp"

#include "Crypto.h"
#include "Identity.h"

#include <stdexcept>

namespace bazarish::client {

namespace {

// libi2pd's global crypto state must be initialised once before any key
// operation. The guard makes repeated calls safe; we never tear it down (the
// cost is negligible and TerminateCrypto mid-process is unsafe under
// concurrent use).
void ensureCryptoInit()
{
    static const bool kInitialised = []() {
        i2p::crypto::InitCrypto(false);
        return true;
    }();
    (void)kInitialised;
}

i2p::data::PrivateKeys parsePrivateKeys(const Bytes& privateKeys)
{
    ensureCryptoInit();
    i2p::data::PrivateKeys keys;
    if (privateKeys.empty() || keys.FromBuffer(privateKeys.data(), privateKeys.size()) == 0) {
        throw std::runtime_error("malformed I2P private keys");
    }
    return keys;
}

Bytes serialize(const i2p::data::PrivateKeys& keys)
{
    Bytes out(keys.GetFullLen());
    const size_t written = keys.ToBuffer(out.data(), out.size());
    out.resize(written);
    return out;
}

}  // namespace

I2pMasterKey generateI2pMaster()
{
    ensureCryptoInit();
    const i2p::data::PrivateKeys keys
        = i2p::data::PrivateKeys::CreateRandomKeys(i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
    I2pMasterKey master;
    master.privateKeys = serialize(keys);
    master.base32 = keys.GetPublic()->GetIdentHash().ToBase32();
    return master;
}

I2pMasterKey loadI2pMaster(const Bytes& privateKeysDat)
{
    const i2p::data::PrivateKeys keys = parsePrivateKeys(privateKeysDat);
    if (keys.GetPublic()->GetSigningKeyType() != i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519) {
        throw std::runtime_error("I2P key is not an Ed25519 (signing type 7) destination");
    }
    I2pMasterKey master;
    master.privateKeys = serialize(keys);
    master.base32 = keys.GetPublic()->GetIdentHash().ToBase32();
    return master;
}

std::string i2pBase32(const Bytes& privateKeys)
{
    const i2p::data::PrivateKeys keys = parsePrivateKeys(privateKeys);
    return keys.GetPublic()->GetIdentHash().ToBase32();
}

std::string i2pPrivateKeysBase64(const Bytes& privateKeys)
{
    const i2p::data::PrivateKeys keys = parsePrivateKeys(privateKeys);
    return keys.ToBase64();
}

Bytes issueI2pOfflineKeys(const Bytes& masterPrivateKeys, std::int64_t expiresUnix)
{
    i2p::data::PrivateKeys master = parsePrivateKeys(masterPrivateKeys);
    const i2p::data::PrivateKeys offline = master.CreateOfflineKeys(
        i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, static_cast<uint32_t>(expiresUnix));
    return serialize(offline);
}

}  // namespace bazarish::client
