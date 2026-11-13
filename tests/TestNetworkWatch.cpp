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
    int losses = 0;
    NetworkWatch watch(
        [&losses]() {
            ++losses;
            bazarish::i2p::networkLost();
        },
        []() { bazarish::i2p::networkChanged(); });
    const std::uint64_t before = bazarish::i2p::networkChanges();
    const auto moved = [before]() { return bazarish::i2p::networkChanges() - before; };

    watch.reachability(Reachability::Online);
    watch.medium(Medium::Ethernet);
    CHECK(moved() == 0);

    watch.reachability(Reachability::Online);
    watch.medium(Medium::Ethernet);
    CHECK(moved() == 0);
    CHECK(losses == 0);

    watch.reachability(Reachability::Disconnected);
    CHECK(moved() == 0);
    CHECK(losses == 1);
    CHECK(bazarish::i2p::networkDown());
    watch.medium(Medium::Unknown);
    CHECK(moved() == 0);
    watch.reachability(Reachability::Online);
    CHECK(moved() == 1);
    CHECK(!bazarish::i2p::networkDown());

    watch.medium(Medium::WiFi);
    CHECK(moved() == 2);
    watch.medium(Medium::WiFi);
    CHECK(moved() == 2);

    watch.reachability(Reachability::Local);
    CHECK(moved() == 3);
    CHECK(losses == 1);

    std::printf("TestNetworkWatch ok\n");
    return 0;
}
