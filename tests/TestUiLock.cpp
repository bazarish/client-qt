// Bazarish project (c) 2026
#include "UiLock.hpp"
#include "TestUtil.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>

using namespace bazarish::app;
using std::chrono::seconds;

namespace {

constexpr const char* kPin = "2580";
constexpr const char* kWrong = "1111";
constexpr std::int64_t kFirstMiss = 1'800'000'000;

}  // namespace

int main()
try {
    CHECK(isUiLockPin("0"));
    CHECK(isUiLockPin("123456"));
    CHECK(!isUiLockPin(""));
    CHECK(!isUiLockPin("1234567"));
    CHECK(!isUiLockPin("12a4"));
    CHECK(!isUiLockPin("\xef\xbc\x91"));
    CHECK_THROWS(uiLockPinOf("12345678"));
    CHECK(isUiAutoLockChoice(0) && isUiAutoLockChoice(3) && isUiAutoLockChoice(10800));
    CHECK(!isUiAutoLockChoice(5) && !isUiAutoLockChoice(180));

    const UiLockPin pin = uiLockPinOf(kPin);
    CHECK(isUiLockPinRecord(pin));
    CHECK(uiLockPinMatches(pin, kPin));
    CHECK(!uiLockPinMatches(pin, kWrong));
    CHECK(!uiLockPinMatches(pin, "258"));
    CHECK(uiLockPinOf(kPin).salt != pin.salt);

    CHECK(!UiLock(std::nullopt).locked());
    UiLock lock(pin);
    CHECK(lock.enabled() && lock.locked());

    const UiLock::Clock::time_point start = UiLock::Clock::now();
    CHECK(lock.tryPin(kWrong, start) == UiLock::Answer::eRefused);
    CHECK(!lock.waitUntil().has_value());
    CHECK(lock.tryPin(kWrong, start) == UiLock::Answer::eRefused);
    CHECK(lock.waitUntil() == start + seconds(2));
    CHECK(lock.tryPin(kPin, start + seconds(1)) == UiLock::Answer::eWaiting);
    CHECK(lock.locked() && lock.failures() == 2);

    UiLock::Clock::time_point at = start + seconds(2);
    for (const int pause : {5, 10, 120, 120, 120}) {
        CHECK(lock.tryPin(kWrong, at) == UiLock::Answer::eRefused);
        CHECK(lock.waitUntil() == at + seconds(pause));
        at += seconds(pause);
    }
    CHECK(lock.failures() == 7);
    CHECK(lock.tryPin(kPin, at) == UiLock::Answer::eUnlocked);
    CHECK(!lock.locked() && lock.failures() == 0 && !lock.waitUntil().has_value());

    lock.lock();
    CHECK(lock.tryPin(kWrong, at) == UiLock::Answer::eRefused);
    CHECK(lock.tryPin(kWrong, at) == UiLock::Answer::eRefused);
    lock.unlockByOwner();
    CHECK(!lock.locked() && lock.failures() == 0 && !lock.waitUntil().has_value());

    lock.disable();
    lock.lock();
    CHECK(!lock.enabled() && !lock.locked());

    const testutil::ScratchDir folder("uilock");
    {
        IntruderNote note(folder.path());
        note.load();
        CHECK(note.empty());
        note.keepPhoto({0xff, 0xd8}, kFirstMiss);
        CHECK(note.photo().empty());
        CHECK(!note.noteFailure(1, kFirstMiss));
        CHECK(!note.noteFailure(2, kFirstMiss));
        CHECK(note.empty());
        CHECK(note.noteFailure(3, kFirstMiss));
        CHECK(note.count() == 3);
        note.keepPhoto({0xff, 0xd8, 0xff}, kFirstMiss + 5);
        note.keepPhoto({0x00}, kFirstMiss + 9);
        CHECK(!note.noteFailure(1, kFirstMiss + 60));
        CHECK(note.count() == 4 && note.lastSeconds() == kFirstMiss + 60);
    }
    {
        IntruderNote again(folder.path());
        again.load();
        CHECK(again.count() == 4 && again.lastSeconds() == kFirstMiss + 60);
        CHECK((again.photo() == bazarish::Bytes{0xff, 0xd8, 0xff}));
        CHECK(again.photoSeconds() == kFirstMiss + 5);
        again.clear();
    }
    CHECK(std::filesystem::is_empty(folder.path()));
    {
        IntruderNote cleared(folder.path());
        cleared.load();
        CHECK(cleared.empty() && cleared.photo().empty());
    }

    std::fprintf(stderr, "TestUiLock passed\n");
    return 0;
} catch (...) {
    return testutil::failed();
}
