// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace bazarish::app {

inline constexpr std::size_t kUiLockPinMaxDigits = 6;
inline constexpr int kUiAutoLockChoices[] = {0, 1, 10, 30, 60, 120, 180};
inline constexpr int kIntruderAfterFailures = 3;

struct UiLockPin {
    Bytes salt;
    Bytes hash;
};

bool isUiLockPin(const std::string& digits);
bool isUiLockPinRecord(const UiLockPin& pin);
bool isUiAutoLockChoice(int minutes);
UiLockPin uiLockPinOf(const std::string& digits);
bool uiLockPinMatches(const UiLockPin& pin, const std::string& digits);

class UiLock {
public:
    using Clock = std::chrono::steady_clock;
    enum class Answer { eUnlocked, eRefused, eWaiting };

    explicit UiLock(std::optional<UiLockPin> pin);

    bool enabled() const { return pin_.has_value(); }
    bool locked() const { return locked_; }
    int failures() const { return failures_; }
    std::optional<Clock::time_point> waitUntil() const { return waitUntil_; }

    void enable(UiLockPin pin);
    void disable();
    void lock();
    Answer tryPin(const std::string& digits, Clock::time_point now);
    void unlockByOwner();

private:
    void unlock();

    std::optional<UiLockPin> pin_;
    bool locked_ = false;
    int failures_ = 0;
    std::optional<Clock::time_point> waitUntil_;
};

class IntruderNote {
public:
    explicit IntruderNote(std::filesystem::path folder);

    int count() const { return count_; }
    std::int64_t lastSeconds() const { return lastSeconds_; }
    const Bytes& photo() const { return photo_; }
    bool empty() const { return count_ == 0; }

    void load();
    // True when this failure starts the note, which is when a photograph is wanted.
    bool noteFailure(int failuresInRow, std::int64_t atSeconds);
    void keepPhoto(Bytes jpeg);
    void clear();

private:
    std::filesystem::path recordFile() const;
    std::filesystem::path photoFile() const;
    void save() const;

    std::filesystem::path folder_;
    int count_ = 0;
    std::int64_t lastSeconds_ = 0;
    Bytes photo_;
};

}  // namespace bazarish::app
