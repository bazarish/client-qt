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

const std::function<bool()> kNothingNewer = []() { return false; };

std::chrono::milliseconds since(const Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
}

struct Awaited {
    MailWaitCause cause = MailWaitCause::eStopped;
    std::chrono::milliseconds took{0};
};

Awaited failedAsk(MailWaitGate& gate, const std::function<void()>& meanwhile,
    const std::function<bool()>& facadeAnew = kNothingNewer)
{
    gate.asking();
    std::thread event([&meanwhile]() {
        std::this_thread::sleep_for(kEventAfter);
        meanwhile();
    });
    const Clock::time_point start = Clock::now();
    Awaited awaited;
    awaited.cause = gate.awaitCause(facadeAnew);
    awaited.took = since(start);
    event.join();
    return awaited;
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

    std::printf("TestMailWaitGate ok\n");
    return 0;
}
