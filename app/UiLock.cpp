// Bazarish project (c) 2026
#include "UiLock.hpp"

#include <bazarish/Argon2.hpp>
#include <bazarish/PrivateFile.hpp>

#include <nlohmann/json.hpp>

#include <openssl/crypto.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace bazarish::app {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;
// OWASP's minimum Argon2id setting: the hash only has to keep the PIN out of the settings text.
constexpr Argon2Cost kPinCost{19 * 1024, 2, 1};

constexpr std::array<std::chrono::seconds, 4> kPauses{
    std::chrono::seconds(2), std::chrono::seconds(5), std::chrono::seconds(10),
    std::chrono::seconds(120)};
constexpr int kFreeFailures = 1;

constexpr const char* kRecordName = "intruder.json";
constexpr const char* kPhotoName = "intruder.jpg";
constexpr fs::perms kOwnerOnly = fs::perms::owner_read | fs::perms::owner_write;

Bytes hashOf(const std::string& digits, const Bytes& salt)
{
    Bytes pass(digits.begin(), digits.end());
    Bytes hash = argon2id(pass, salt, kPinCost, kHashBytes);
    OPENSSL_cleanse(pass.data(), pass.size());
    return hash;
}

void removeIfThere(const fs::path& file)
{
    std::error_code error;
    fs::remove(file, error);
    if (error) {
        throw std::runtime_error("cannot remove " + file.string() + ": " + error.message());
    }
}

}  // namespace

bool isUiLockPin(const std::string& digits)
{
    return !digits.empty() && digits.size() <= kUiLockPinMaxDigits
        && std::all_of(digits.begin(), digits.end(), [](const char c) { return c >= '0' && c <= '9'; });
}

bool isUiLockPinRecord(const UiLockPin& pin)
{
    return pin.salt.size() == kSaltBytes && pin.hash.size() == kHashBytes;
}

bool isUiAutoLockChoice(const int minutes)
{
    return std::find(std::begin(kUiAutoLockChoices), std::end(kUiAutoLockChoices), minutes)
        != std::end(kUiAutoLockChoices);
}

UiLockPin uiLockPinOf(const std::string& digits)
{
    if (!isUiLockPin(digits)) {
        throw std::invalid_argument("an app lock PIN is 1 to 6 digits");
    }
    UiLockPin pin;
    pin.salt = randomBytes(kSaltBytes);
    pin.hash = hashOf(digits, pin.salt);
    return pin;
}

bool uiLockPinMatches(const UiLockPin& pin, const std::string& digits)
{
    if (!isUiLockPin(digits)) {
        return false;
    }
    const Bytes hash = hashOf(digits, pin.salt);
    return hash.size() == pin.hash.size()
        && CRYPTO_memcmp(hash.data(), pin.hash.data(), hash.size()) == 0;
}

UiLock::UiLock(std::optional<UiLockPin> pin)
    : pin_(std::move(pin))
    , locked_(pin_.has_value())
{
}

void UiLock::enable(UiLockPin pin)
{
    pin_ = std::move(pin);
    unlock();
}

void UiLock::disable()
{
    pin_.reset();
    unlock();
}

void UiLock::lock()
{
    locked_ = enabled();
}

UiLock::Answer UiLock::tryPin(const std::string& digits, const Clock::time_point now)
{
    if (!locked_) {
        return Answer::eUnlocked;
    }
    if (waitUntil_.has_value() && now < *waitUntil_) {
        return Answer::eWaiting;
    }
    if (uiLockPinMatches(*pin_, digits)) {
        unlock();
        return Answer::eUnlocked;
    }
    ++failures_;
    if (failures_ > kFreeFailures) {
        const std::size_t step = std::min(
            static_cast<std::size_t>(failures_ - kFreeFailures - 1), kPauses.size() - 1);
        waitUntil_ = now + kPauses[step];
    }
    return Answer::eRefused;
}

void UiLock::unlockByOwner()
{
    if (locked_) {
        unlock();
    }
}

void UiLock::unlock()
{
    locked_ = false;
    failures_ = 0;
    waitUntil_.reset();
}

IntruderNote::IntruderNote(fs::path folder)
    : folder_(std::move(folder))
{
}

fs::path IntruderNote::recordFile() const
{
    return folder_ / kRecordName;
}

fs::path IntruderNote::photoFile() const
{
    return folder_ / kPhotoName;
}

void IntruderNote::load()
{
    count_ = 0;
    lastSeconds_ = 0;
    photo_.clear();
    if (!fs::exists(recordFile())) {
        removeIfThere(photoFile());
        return;
    }
    const nlohmann::json record = nlohmann::json::parse(readFileText(recordFile()));
    const int count = record.at("count").get<int>();
    const std::int64_t last = record.at("last").get<std::int64_t>();
    if (count < kIntruderAfterFailures || last <= 0) {
        throw std::runtime_error(recordFile().string() + " names no failed unlock");
    }
    count_ = count;
    lastSeconds_ = last;
    if (fs::exists(photoFile())) {
        photo_ = readFileBytes(photoFile());
    }
}

bool IntruderNote::noteFailure(const int failuresInRow, const std::int64_t atSeconds)
{
    const bool starts = empty();
    if (starts && failuresInRow < kIntruderAfterFailures) {
        return false;
    }
    count_ = starts ? failuresInRow : count_ + 1;
    lastSeconds_ = atSeconds;
    save();
    return starts;
}

void IntruderNote::keepPhoto(Bytes jpeg)
{
    if (empty() || !photo_.empty() || jpeg.empty()) {
        return;
    }
    writeFileAtomic(photoFile(),
        std::string_view(reinterpret_cast<const char*>(jpeg.data()), jpeg.size()), kOwnerOnly);
    photo_ = std::move(jpeg);
}

void IntruderNote::clear()
{
    removeIfThere(photoFile());
    removeIfThere(recordFile());
    count_ = 0;
    lastSeconds_ = 0;
    photo_.clear();
}

void IntruderNote::save() const
{
    writePrivateFile(recordFile(), nlohmann::json{{"count", count_}, {"last", lastSeconds_}}.dump());
}

}  // namespace bazarish::app
