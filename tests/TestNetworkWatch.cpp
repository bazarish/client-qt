// Bazarish project (c) 2026
#include "NetworkWatch.hpp"

#include "TestUtil.hpp"

#include <bazarish/I2p.hpp>

#include <cstdint>
#include <cstdio>

using bazarish::app::NetworkWatch;
using Reachability = QNetworkInformation::Reachability;
using Medium = QNetworkInformation::TransportMedium;

int main()
{
    NetworkWatch watch([]() { bazarish::i2p::networkChanged(); });
    const std::uint64_t before = bazarish::i2p::networkChanges();
    const auto moved = [before]() { return bazarish::i2p::networkChanges() - before; };

    watch.reachability(Reachability::Online);
    watch.medium(Medium::Ethernet);
    CHECK(moved() == 0);

    watch.reachability(Reachability::Online);
    watch.medium(Medium::Ethernet);
    CHECK(moved() == 0);

    watch.reachability(Reachability::Disconnected);
    CHECK(moved() == 1);
    watch.reachability(Reachability::Online);
    CHECK(moved() == 2);

    watch.medium(Medium::WiFi);
    CHECK(moved() == 3);
    watch.medium(Medium::WiFi);
    CHECK(moved() == 3);

    std::printf("TestNetworkWatch ok\n");
    return 0;
}
