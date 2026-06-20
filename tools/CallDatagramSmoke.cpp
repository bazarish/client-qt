// Bazarish project (c) 2026
//
// Manual live smoke (NOT part of ctest): proves SAM v3 RAW datagrams round-trip
// over a real router - the transport that carries call audio. Brings up two RAW
// datagram destinations on the local SAM bridge and sends datagrams from A to
// B's blinded b33 routing address, confirming the router forwards them back.
//
//   call-datagram-smoke [control-port] [udp-port]
//
// Defaults to 127.0.0.1:7656 control / 7655 udp (i2pd's SAM defaults).
#include <bazarish/Sam.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace bazarish;

int main(int argc, char** argv)
{
    const std::uint16_t control
        = argc > 1 ? static_cast<std::uint16_t>(std::stoi(argv[1])) : SamClient::kDefaultPort;
    const std::uint16_t udp
        = argc > 2 ? static_cast<std::uint16_t>(std::stoi(argv[2])) : kDefaultSamUdpPort;
    // Leaseset type to publish the media destinations as: "b32" (standard, works
    // on any SAM) or "b33" (encrypted). Routing a datagram to a b33 host needs a
    // SAM bridge that resolves blinded addresses for datagrams (i2pd patch).
    const std::string lsArg = argc > 3 ? argv[3] : "b32";
    const int leaseSetType = (lsArg == "b32") ? kStandardLeaseSetType : kEncryptedLeaseSetType;

    std::printf("bringing up two RAW datagram sessions (SAM %u, udp %u, %s)...\n", control, udp,
        lsArg.c_str());
    SamDatagramSession a(
        "127.0.0.1", control, udp, "bz-dg-a", "TRANSIENT", leaseSetType);
    SamDatagramSession b(
        "127.0.0.1", control, udp, "bz-dg-b", "TRANSIENT", leaseSetType);
    std::printf("A routes at %s\n", a.routingAddress().c_str());
    std::printf("B routes at %s\n", b.routingAddress().c_str());

    // Let both encrypted leasesets publish before sending.
    std::printf("waiting 30s for leaseset publication...\n");
    std::this_thread::sleep_for(std::chrono::seconds(30));

    std::atomic<int> received{0};
    std::thread receiver([&] {
        for (int i = 0; i < 120 && received.load() < 5; ++i) {
            const std::vector<std::uint8_t> packet = b.receive(1000);
            if (!packet.empty()) {
                received.fetch_add(1);
                std::printf("B received datagram (%zu bytes)\n", packet.size());
            }
        }
    });

    for (int i = 0; i < 60 && received.load() < 5; ++i) {
        const std::string message = "bazarish-call-datagram-" + std::to_string(i);
        try {
            a.send(b.routingAddress(), message.data(), message.size());
        } catch (const std::exception& error) {
            std::printf("send failed: %s\n", error.what());
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    receiver.join();

    if (received.load() > 0) {
        std::printf("RESULT: OK - %d datagrams routed A->B over I2P\n", received.load());
        return 0;
    }
    std::printf("RESULT: FAIL - no datagrams arrived\n");
    return 1;
}
