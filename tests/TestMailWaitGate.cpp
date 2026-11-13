// Bazarish project (c) 2026
#include "MailWaitGate.hpp"

#include "TestUtil.hpp"

#include <bazarish/I2p.hpp>

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>

using namespace bazarish;
using namespace bazarish::client;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::chrono::milliseconds kStaleBound{400};
constexpr std::chrono::milliseconds kEventAfter{150};
constexpr int kFailedAsks = 5;
constexpr ServerDownPauses kServerDown{std::chrono::milliseconds{100},
    std::chrono::milliseconds{250}, std::chrono::milliseconds{450}};
constexpr std::chrono::milliseconds kPauseSlack{100};
constexpr int kSteadyServerDownAsks = 2;

const std::function<bool()> kNothingNewer = []() { return false; };

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
    const std::function<bool()>& facadeAnew = kNothingNewer)
{
    return awaitedWhile(
        gate, meanwhile, [&gate, &facadeAnew]() { return gate.awaitCause(facadeAnew); });
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
    return awaited.cause == cause && awaited.took >= kEventAfter && awaited.took < kStaleBound;
}

}  // namespace

int main()
{
    {
        MailWaitGate gate(kStaleBound);
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
        MailWaitGate gate(kStaleBound);
        gate.serverAnswered();
        gate.registered();
        gate.asking();
        const Clock::time_point start = Clock::now();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eStaleLeaseSet);
        CHECK(since(start) >= kStaleBound);
    }

    {
        MailWaitGate gate(kStaleBound);
        int lookups = 0;
        gate.asking();
        const Clock::time_point start = Clock::now();
        CHECK(gate.awaitCause([&lookups]() {
            ++lookups;
            return true;
        }) == MailWaitCause::eNewerLeaseSet);
        CHECK(since(start) < kEventAfter);
        CHECK(lookups == 1);
        gate.asking();
        CHECK(gate.awaitCause([&lookups]() {
            ++lookups;
            return false;
        }) == MailWaitCause::eStaleLeaseSet);
        CHECK(lookups == 2);
    }

    {
        MailWaitGate gate(kStaleBound);
        gate.asking();
        gate.answered();
        gate.asking();
        const Clock::time_point start = Clock::now();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eHeldWaitBroke);
        CHECK(since(start) < kEventAfter);
        gate.asking();
        gate.answered();
        gate.asking();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eStaleLeaseSet);
        gate.asking();
        gate.answered();
        gate.asking();
        CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eHeldWaitBroke);
    }

    {
        MailWaitGate gate(kStaleBound);
        const Clock::time_point start = Clock::now();
        for (int i = 0; i < kFailedAsks; ++i) {
            gate.asking();
            CHECK(gate.awaitCause(kNothingNewer) == MailWaitCause::eStaleLeaseSet);
        }
        CHECK(since(start) >= kStaleBound * kFailedAsks);
    }

    {
        MailWaitGate gate(kStaleBound, kServerDown);
        CHECK(pausedFor(gate, kServerDown.first));
        CHECK(pausedFor(gate, kServerDown.second));
        for (int i = 0; i < kSteadyServerDownAsks; ++i) {
            CHECK(pausedFor(gate, kServerDown.steady));
        }
        gate.answered();
        CHECK(pausedFor(gate, kServerDown.first));
        CHECK(pausedFor(gate, kServerDown.second));
        gate.asking();
        CHECK(gate.awaitCause([]() { return true; }) == MailWaitCause::eNewerLeaseSet);
        CHECK(pausedFor(gate, kServerDown.first));
        gate.restart();
        CHECK(pausedFor(gate, kServerDown.first));
    }

    {
        const ServerDownPauses longPauses{kStaleBound, kStaleBound, kStaleBound};
        MailWaitGate gate(kStaleBound, longPauses);
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

    std::printf("TestMailWaitGate ok\n");
    return 0;
}
