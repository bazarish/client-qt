// Bazarish project (c) 2026
#include "MailWaitGate.hpp"

#include "TestUtil.hpp"

#include <bazarish/I2p.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <utility>

using namespace bazarish;
using namespace bazarish::client;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::chrono::milliseconds kAsk{400};
constexpr std::chrono::milliseconds kMissing{600};
constexpr SilencePauses kSilence{kAsk, kMissing, std::chrono::hours{1}, std::chrono::hours{1}};
constexpr SilencePauses kQuickSilence{std::chrono::milliseconds{50}, std::chrono::milliseconds{50},
    std::chrono::milliseconds{300}, std::chrono::milliseconds{500}};
constexpr std::chrono::milliseconds kEventAfter{150};
constexpr int kMissingLookups = 2;
constexpr int kPauses = 20;
constexpr std::chrono::milliseconds kWakeSlack{100};
constexpr ServerDownPauses kServerDown{std::chrono::milliseconds{100},
    std::chrono::milliseconds{250}, std::chrono::milliseconds{450}};
constexpr std::chrono::milliseconds kPauseSlack{100};
constexpr int kSteadyServerDownAsks = 2;

const FacadeLink kLink{"facade-link.b32.i2p", "facade.b32.i2p"};
const FacadeLookups kNothingNewer{[]() { return FacadeLookup::eSame; }, kLink};

FacadeLookups lookingUp(std::function<FacadeLookup()> lookUp)
{
    return FacadeLookups{std::move(lookUp), kLink};
}

i2p::AskedLookup askedOn(const FacadeLink& link, const bool newer)
{
    i2p::LeaseSetRefresh refresh;
    refresh.before = i2p::LeaseSetSeen{1, 2};
    refresh.after = i2p::LeaseSetSeen{newer ? 2 : 1, 2};
    return i2p::AskedLookup{link.localHost, link.remoteHost, refresh};
}

std::chrono::milliseconds since(const Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
}

struct Awaited {
    MailWaitCause cause = MailWaitCause::eStopped;
    std::chrono::milliseconds took{0};
};

Awaited awaitedWhile(MailWaitGate& gate, const std::function<void()>& meanwhile,
    const std::function<MailWaitCause()>& await)
{
    gate.asking();
    std::thread event([&meanwhile]() {
        std::this_thread::sleep_for(kEventAfter);
        meanwhile();
    });
    const Clock::time_point start = Clock::now();
    Awaited awaited;
    awaited.cause = await();
    awaited.took = since(start);
    event.join();
    return awaited;
}

Awaited failedAsk(MailWaitGate& gate, const std::function<void()>& meanwhile,
    const FacadeLookups& facade = kNothingNewer)
{
    return awaitedWhile(gate, meanwhile, [&gate, &facade]() { return gate.awaitCause(facade); });
}

Awaited serverDownAsk(MailWaitGate& gate, const std::function<void()>& meanwhile)
{
    return awaitedWhile(gate, meanwhile, [&gate]() { return gate.awaitServer(); });
}

bool pausedFor(MailWaitGate& gate, const std::chrono::milliseconds pause)
{
    gate.asking();
    const Clock::time_point start = Clock::now();
    const MailWaitCause cause = gate.awaitServer();
    const std::chrono::milliseconds took = since(start);
    std::printf("%s after %lld ms\n", std::string(mailWaitCauseName(cause)).c_str(),
        static_cast<long long>(took.count()));
    return cause == MailWaitCause::eServerPauseOver && took >= pause && took < pause + kPauseSlack;
}

bool tookTheEvent(const Awaited& awaited, const MailWaitCause cause)
{
    std::printf("%s after %lld ms\n", std::string(mailWaitCauseName(awaited.cause)).c_str(),
        static_cast<long long>(awaited.took.count()));
    return awaited.cause == cause && awaited.took >= kEventAfter && awaited.took < kAsk;
}

}  // namespace

int main()
try {
    {
        MailWaitGate gate(kSilence);
        CHECK(tookTheEvent(failedAsk(gate, [&gate]() { gate.serverAnswered(); }),
            MailWaitCause::eServerAnswered));
        CHECK(tookTheEvent(failedAsk(gate, [&gate]() { gate.registered(); }),
            MailWaitCause::eRegistered));
        CHECK(tookTheEvent(failedAsk(gate, []() { i2p::networkChanged(); }),
            MailWaitCause::eLinkBack));
        CHECK(tookTheEvent(failedAsk(gate, [&gate]() { gate.stop(); }), MailWaitCause::eStopped));
        gate.asking();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eStopped);
    }

    {
        MailWaitGate gate(kSilence);
        gate.serverAnswered();
        gate.registered();
        int lookups = 0;
        gate.asking();
        const Clock::time_point start = Clock::now();
        CHECK(gate.awaitCause(lookingUp([&lookups]() {
            ++lookups;
            return FacadeLookup::eSame;
        })) == MailWaitCause::eSilencePauseOver);
        CHECK(since(start) >= 2 * kAsk);
        CHECK(since(start) < 3 * kAsk);
        CHECK(lookups == 1);
    }

    {
        MailWaitGate gate(kSilence);
        int lookups = 0;
        gate.asking();
        Clock::time_point start = Clock::now();
        CHECK(gate.awaitCause(lookingUp([&lookups]() {
            ++lookups;
            return FacadeLookup::eNewer;
        })) == MailWaitCause::eNewerLeaseSet);
        CHECK(since(start) >= kAsk);
        CHECK(since(start) < 2 * kAsk);
        CHECK(lookups == 1);

        lookups = 0;
        gate.asking();
        start = Clock::now();
        CHECK(gate.awaitCause(lookingUp([&lookups]() {
            ++lookups;
            return lookups <= kMissingLookups ? FacadeLookup::eMissing : FacadeLookup::eSame;
        })) == MailWaitCause::eSilencePauseOver);
        const std::chrono::milliseconds missingCycle = 2 * kAsk + kMissingLookups * kMissing;
        CHECK(since(start) >= missingCycle);
        CHECK(since(start) < missingCycle + kAsk);
        CHECK(lookups == kMissingLookups + 1);
    }

    {
        MailWaitGate gate(kSilence);
        gate.asking();
        gate.answered();
        gate.asking();
        const Clock::time_point start = Clock::now();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eHeldWaitBroke);
        CHECK(since(start) < kEventAfter);
        gate.asking();
        gate.answered();
        gate.asking();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eSilencePauseOver);
        gate.asking();
        gate.answered();
        gate.asking();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eHeldWaitBroke);
    }

    {
        MailWaitGate gate(kSilence);
        int lookups = 0;
        const FacadeLookups counted = lookingUp([&lookups]() {
            ++lookups;
            return FacadeLookup::eSame;
        });
        i2p::lookupAsked(askedOn(kLink, true));
        const Awaited newer
            = failedAsk(gate, []() { i2p::lookupAsked(askedOn(kLink, true)); }, counted);
        CHECK(tookTheEvent(newer, MailWaitCause::eNewerLeaseSet));
        const Awaited same
            = failedAsk(gate, []() { i2p::lookupAsked(askedOn(kLink, false)); }, counted);
        CHECK(same.cause == MailWaitCause::eSilencePauseOver);
        CHECK(same.took >= kEventAfter + kAsk);
        CHECK(same.took < 2 * kAsk);
        CHECK(lookups == 0);
        const FacadeLink elsewhere{"another-link.b32.i2p", kLink.remoteHost};
        const Awaited ignored
            = failedAsk(gate, [&elsewhere]() { i2p::lookupAsked(askedOn(elsewhere, true)); },
                counted);
        CHECK(ignored.cause == MailWaitCause::eSilencePauseOver);
        CHECK(ignored.took >= 2 * kAsk);
        CHECK(lookups == 1);
    }

    {
        MailWaitGate gate(kQuickSilence);
        const Clock::time_point silent = Clock::now();
        while (since(silent) < kQuickSilence.longSilence) {
            gate.asking();
            CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eSilencePauseOver);
        }
        gate.asking();
        Clock::time_point start = Clock::now();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eSilencePauseOver);
        CHECK(since(start) >= 2 * kQuickSilence.longPause);
        gate.asking();
        gate.answered();
        gate.asking();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eHeldWaitBroke);
        gate.asking();
        start = Clock::now();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eSilencePauseOver);
        CHECK(since(start) < kQuickSilence.longPause);
    }

    {
        MailWaitGate gate(kSilence, kServerDown);
        CHECK(pausedFor(gate, kServerDown.first));
        CHECK(pausedFor(gate, kServerDown.second));
        for (int i = 0; i < kSteadyServerDownAsks; ++i) {
            CHECK(pausedFor(gate, kServerDown.steady));
        }
        gate.answered();
        CHECK(pausedFor(gate, kServerDown.first));
        CHECK(pausedFor(gate, kServerDown.second));
        gate.asking();
        CHECK(gate.awaitCause(lookingUp([]() { return FacadeLookup::eNewer; }))
            == MailWaitCause::eNewerLeaseSet);
        CHECK(pausedFor(gate, kServerDown.first));
        gate.restart();
        CHECK(pausedFor(gate, kServerDown.first));
    }

    {
        const ServerDownPauses longPauses{kAsk, kAsk, kAsk};
        MailWaitGate gate(kSilence, longPauses);
        CHECK(tookTheEvent(serverDownAsk(gate, [&gate]() { gate.serverAnswered(); }),
            MailWaitCause::eServerAnswered));
        CHECK(tookTheEvent(serverDownAsk(gate, [&gate]() { gate.registered(); }),
            MailWaitCause::eRegistered));
        CHECK(tookTheEvent(serverDownAsk(gate, []() { i2p::networkChanged(); }),
            MailWaitCause::eLinkBack));
        CHECK(tookTheEvent(serverDownAsk(gate, [&gate]() { gate.stop(); }), MailWaitCause::eStopped));
        gate.asking();
        CHECK(gate.awaitServer() == MailWaitCause::eStopped);
    }

    {
        MailWaitGate gate;
        std::chrono::steady_clock::duration shortest = std::chrono::hours{1};
        std::chrono::steady_clock::duration longest{};
        for (int i = 0; i < kPauses; ++i) {
            const auto from = std::chrono::steady_clock::now();
            CHECK(gate.pausedAfterAnswer());
            const auto took = std::chrono::steady_clock::now() - from;
            shortest = std::min(shortest, took);
            longest = std::max(longest, took);
        }
        CHECK(longest <= kLongestReaskPause + kWakeSlack);
        CHECK(shortest < longest && longest >= kWakeSlack);
        gate.stop();
        const auto from = std::chrono::steady_clock::now();
        CHECK(!gate.pausedAfterAnswer());
        CHECK(std::chrono::steady_clock::now() - from < kWakeSlack);
    }

    std::printf("TestMailWaitGate ok\n");
    return 0;
} catch (...) {
    return testutil::failed();
}
