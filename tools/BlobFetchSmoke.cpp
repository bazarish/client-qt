// Bazarish project (c) 2026
//
// Manual smoke (NOT run by ctest): fetches a path from a .b32.i2p blob host over
// a transient SAM session and prints the HTTP status + body size. Proves the
// transient-SAM connect-to-b33 + HTTP-over-stream download path against a live
// router and a running blob-storage daemon.
//
//   ./blob-fetch-smoke <b33host> <path> [sam-host] [sam-port]
#include "BlobTransport.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

int main(const int argc, const char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: blob-fetch-smoke <b33host> <path> [sam-host] [sam-port]\n");
        return 2;
    }
    const std::string host = argv[1];
    const std::string path = argv[2];
    const std::string samHost = argc > 3 ? argv[3] : "127.0.0.1";
    const std::uint16_t samPort
        = static_cast<std::uint16_t>(argc > 4 ? std::stoi(argv[4]) : 7656);

    try {
        std::printf("fetching %s%s over I2P (transient SAM; tunnel build + blinded "
                    "lookup may take ~tens of s)...\n",
            host.c_str(), path.c_str());
        std::fflush(stdout);
        const bazarish::client::I2pHttpResponse response
            = bazarish::client::i2pRequest(samHost, samPort, host, "GET", path);
        std::printf("status=%d body=%zu bytes\n", response.status, response.body.size());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fetch failed: %s\n", error.what());
        return 1;
    }
}
