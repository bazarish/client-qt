// Bazarish project (c) 2026
#include "LargeBlob.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Crypto.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

using namespace bazarish;
using namespace bazarish::client;

int main()
{
    Bytes blob;
    for (int i = 0; i < 4096; ++i) {
        blob.push_back(static_cast<unsigned char>(i * 7 + 3));
    }

    // Packing yields opaque ciphertext, a fresh key and a binding digest.
    const PackedBlob packed = packLargeBlob(blob);
    CHECK(!packed.ciphertext.empty());
    CHECK(packed.ciphertext != blob);
    CHECK(packed.fileKey.size() == 64);  // 32 random bytes, hex
    CHECK(packed.size == packed.ciphertext.size());
    CHECK(packed.sha256 == toHex(sha256(packed.ciphertext)));

    // Correct key + digest round-trips to the original.
    CHECK(unpackLargeBlob(packed.ciphertext, packed.fileKey, packed.sha256) == blob);

    // A wrong digest is rejected before decryption.
    bool threw = false;
    try {
        (void)unpackLargeBlob(packed.ciphertext, packed.fileKey, std::string(64, '0'));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    // A tampered byte breaks the digest binding.
    Bytes tampered = packed.ciphertext;
    tampered[tampered.size() / 2] ^= 0x01;
    threw = false;
    try {
        (void)unpackLargeBlob(tampered, packed.fileKey, packed.sha256);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    // A wrong key fails to decrypt (digest still matches the untouched bytes).
    threw = false;
    try {
        (void)unpackLargeBlob(packed.ciphertext, toHex(randomBytes(32)), packed.sha256);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    // Pointer round-trips through JSON.
    BlobPointer pointer;
    pointer.blobUrl = "http://yhfjbu7hkuqyqwp3pvpuf6vk63bdlwtryjcueunavz52ai5y3yuvwdkh.b32.i2p/b/abcd";
    pointer.blobId = "abcd";
    pointer.fileKey = packed.fileKey;
    pointer.sha256 = packed.sha256;
    pointer.size = packed.size;
    pointer.meta = {{"name", "report.pdf"}, {"mime", "application/pdf"}};
    const BlobPointer back = blobPointerFromJson(blobPointerToJson(pointer));
    CHECK(back.blobUrl == pointer.blobUrl);
    CHECK(back.blobId == pointer.blobId);
    CHECK(back.fileKey == pointer.fileKey);
    CHECK(back.sha256 == pointer.sha256);
    CHECK(back.size == pointer.size);
    CHECK(back.meta.at("name").get<std::string>() == "report.pdf");

    std::printf("TestLargeBlob: all checks passed\n");
    return 0;
}
