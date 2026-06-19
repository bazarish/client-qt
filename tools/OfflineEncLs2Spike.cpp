// Bazarish project (c) 2026
//
// Manual spike (NOT run by ctest): proves that an OFFLINE-SIGNATURE I2P
// destination — a user-owned master key with a time-boxed transient delegation,
// exactly the artifact a paid client hands its serving server — can be operated
// over SAM as an ENCRYPTED LeaseSet2 (the b33 invariant) AND is reachable
// inbound on its blinded address from an independent SAM session. This is the
// architectural gate for the per-user paid i2pDest provisioning track.
//
//   ./offline-enc-ls2-spike [host] [port]
#include "I2pKeys.hpp"

#include <bazarish/I2pAddress.hpp>

#include <bazarish/Sam.hpp>

#include <Identity.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

int main(const int argc, const char** argv)
{
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::uint16_t port = static_cast<std::uint16_t>(argc > 2 ? std::stoi(argv[2]) : 7656);
    // "enc" (default) publishes an encrypted LeaseSet2 and reaches it by the
    // blinded b33; "std" publishes a standard LeaseSet2 (type 3) and reaches it
    // by the plain IdentHash b32 — a control to isolate whether the resolve
    // failure is specific to the encrypted LeaseSet.
    const std::string mode = argc > 3 ? argv[3] : "enc";
    const bool standard = mode == "std";
    const int kStandardLeaseSet2 = 3;  // i2p::data::NETDB_STORE_TYPE_STANDARD_LEASESET2
    const int leaseSetType = standard ? kStandardLeaseSet2 : bazarish::kEncryptedLeaseSetType;

    try {
        // 1. User-owned master, then a 7-day offline ("transient") delegation —
        //    the exact blob a client would issue to its serving server.
        const bazarish::client::I2pMasterKey master = bazarish::client::generateI2pMaster();
        const std::int64_t expires = static_cast<std::int64_t>(std::time(nullptr)) + 7 * 86400;
        const bazarish::Bytes transient
            = bazarish::client::issueI2pOfflineKeys(master.privateKeys, expires);

        // 2. Re-parse the transient, confirm it really carries an offline
        //    signature, then serialise to the I2P-base64 the SAM DESTINATION=
        //    field expects (the form the server would feed its router).
        i2p::data::PrivateKeys keys;
        if (keys.FromBuffer(transient.data(), transient.size()) == 0) {
            throw std::runtime_error("failed to parse issued transient");
        }
        if (!keys.IsOfflineSignature()) {
            throw std::runtime_error("issued transient is NOT an offline signature");
        }
        const std::string offlineB64 = keys.ToBase64();

        std::printf("master IdentHash b32: %s.b32.i2p\n", master.base32.c_str());
        std::printf("transient offline:    yes (%zu raw bytes)\n", transient.size());

        // 3. Operate the offline destination over SAM as an ENCRYPTED LeaseSet2.
        //    If i2pd rejected offline + leaseSetType=5 this constructor throws.
        std::printf("creating offline + encrypted-LS2 SAM session (tunnel build ~minutes)...\n");
        std::fflush(stdout);
        std::printf("mode: %s (leaseSetType=%d)\n", mode.c_str(), leaseSetType);
        bazarish::SamSession server(host, port, "bz-offline-enc",
            offlineB64, leaseSetType, bazarish::I2pPrivacy::eMax);

        const std::string destination = server.publicDestination();
        const std::string b33 = bazarish::encryptedLeaseSetHost(destination);
        // In "std" mode reach the destination by its plain IdentHash b32; in
        // "enc" mode by the blinded b33.
        const std::string connectTarget = standard ? (master.base32 + ".b32.i2p") : b33;
        std::printf("SESSION RESULT=OK (offline dest accepted by i2pd)\n");
        std::printf("IdentHash b32 host:   %s.b32.i2p\n", master.base32.c_str());
        std::printf("blinded b33 host:     %s\n", b33.c_str());
        std::printf("connect target:       %s\n", connectTarget.c_str());

        // 4. Decisive inbound proof. The acceptor RE-accepts in a loop: our
        //    SamSession::accept() surfaces the SAM accept timeout as an
        //    exception, so a single accept would die long before a fresh
        //    destination's encrypted LeaseSet propagates. It stops once it has
        //    served one stream or the prober gives up.
        std::atomic<bool> stop{false};
        std::atomic<bool> served{false};
        std::thread acceptor([&server, &stop, &served]() {
            while (!stop.load()) {
                try {
                    std::string peer;
                    bazarish::SamStream stream = server.accept(peer);
                    char byte = 0;
                    stream.readExact(&byte, 1);
                    const char reply = static_cast<char>(byte + 1);
                    stream.writeAll(&reply, 1);
                    stream.shutdownWrite();
                    served.store(true);
                    std::printf("acceptor: served one inbound stream\n");
                    std::fflush(stdout);
                    return;
                } catch (const std::exception&) {
                    // SAM accept timeout / transient error — re-accept unless
                    // the prober has stopped trying.
                }
            }
        });

        std::printf("probe: resolving b33 and connecting (fresh dest, up to ~10 min)...\n");
        std::fflush(stdout);
        bazarish::SamSession probe(host, port, "bz-offline-probe",
            "TRANSIENT", bazarish::kEncryptedLeaseSetType, bazarish::I2pPrivacy::eMax);

        // A fresh destination's encrypted LeaseSet takes a while to land in the
        // netdb; retry the resolve/connect without throwing.
        std::optional<bazarish::SamStream> stream;
        for (int attempt = 1; attempt <= 20 && !stream.has_value(); ++attempt) {
            try {
                stream.emplace(probe.connect(connectTarget));
            } catch (const std::exception& error) {
                std::printf("  connect attempt %d failed (%s); retrying...\n",
                    attempt, error.what());
                std::fflush(stdout);
                std::this_thread::sleep_for(std::chrono::seconds(30));
            }
        }

        int result = 1;
        if (stream.has_value()) {
            const char ping = 41;
            stream->writeAll(&ping, 1);
            char pong = 0;
            stream->readExact(&pong, 1);
            if (pong == ping + 1) {
                std::printf("INBOUND OK: offline destination reachable on its blinded b33 as an encrypted LeaseSet2.\n");
                std::printf("VERDICT: blocker LIFTED — a per-user offline-key dest is served AND reachable as encrypted LS2.\n");
                result = 0;
            } else {
                std::fprintf(stderr, "INBOUND MISMATCH: expected %d, got %d\n", ping + 1, pong);
                result = 2;
            }
        } else {
            std::fprintf(stderr, "INBOUND FAILED: could not resolve/connect the b33 within the retry budget.\n");
            result = 3;
        }

        stop.store(true);
        acceptor.join();
        (void)served;
        return result;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "spike failed: %s\n", error.what());
        return 1;
    }
}
