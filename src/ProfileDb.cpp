// Bazarish project (c) 2026
#include "ProfileDb.hpp"

#include <sqlcipher/sqlite3.h>

#include <stdexcept>

namespace bazarish::client {

namespace {

namespace fs = std::filesystem;

// How long a writer waits for another connection to finish before giving up. The
// transcript and this store are two connections on one file, so a write can meet
// a lock; they are both in-process and short, so the wait is a formality.
constexpr int kBusyTimeoutMs = 5000;

// Key derivation rounds for a profile database. SQLCipher's own default is
// 256000, which costs a quarter of a second per open - paid on every profile the
// picker lists, and worse on a phone. The trade is deliberate: a passphrase is
// that much cheaper to guess offline, so it protects a stolen file only as far as
// the passphrase itself is strong.
constexpr int kKdfIterations = 4000;

// Single quotes double inside a SQL string literal.
std::string quoted(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        out.push_back(c);
        if (c == '\'') {
            out.push_back(c);
        }
    }
    return out;
}

void run(sqlite3* const db, const std::string& sql)
{
    char* message = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string reason = message == nullptr ? "unknown error" : message;
        sqlite3_free(message);
        throw std::runtime_error("profile database: " + reason);
    }
}

// Opens the file and unlocks it. The key pragma has to be the first statement on
// the connection.
sqlite3* openKeyed(const fs::path& file, const std::string& key)
{
    sqlite3* db = nullptr;
    if (sqlite3_open(file.string().c_str(), &db) != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    // SQLCipher reports a failed decryption on stderr; callers report it through
    // their own return values instead.
    sqlite3_exec(db, "PRAGMA cipher_log_level = NONE", nullptr, nullptr, nullptr);
    const std::string pragma = "PRAGMA key = '" + quoted(key) + "'";
    if (sqlite3_exec(db, pragma.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    const std::string rounds = "PRAGMA kdf_iter = " + std::to_string(kKdfIterations);
    if (sqlite3_exec(db, rounds.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    sqlite3_busy_timeout(db, kBusyTimeoutMs);
    // With the wrong key the pages do not decrypt and the first read fails.
    if (sqlite3_exec(db, "SELECT count(*) FROM sqlite_master", nullptr, nullptr, nullptr)
        != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    return db;
}

std::string keyOrDefault(const std::string& passphrase)
{
    return passphrase.empty() ? std::string(ProfileDb::kDefaultKey) : passphrase;
}


}  // namespace

ProfileDb::ProfileDb(const fs::path& file, const std::string& passphrase)
{
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path());
    }
    db_ = openKeyed(file, keyOrDefault(passphrase));
    if (db_ == nullptr) {
        throw std::runtime_error("profile database: wrong passphrase or unreadable file");
    }
    run(db_, "CREATE TABLE IF NOT EXISTS state (name TEXT PRIMARY KEY, value BLOB)");
}

ProfileDb::~ProfileDb()
{
    sqlite3_close(db_);
}

bool ProfileDb::opens(const fs::path& file, const std::string& passphrase)
{
    if (!fs::exists(file)) {
        return false;
    }
    sqlite3* const db = openKeyed(file, keyOrDefault(passphrase));
    sqlite3_close(db);
    return db != nullptr;
}

std::optional<Bytes> ProfileDb::get(const std::string& name) const
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT value FROM state WHERE name = ?", -1, &statement, nullptr)
        != SQLITE_OK) {
        throw std::runtime_error(std::string("profile database: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(statement, 1, name.c_str(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
    std::optional<Bytes> value;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* const data = static_cast<const unsigned char*>(sqlite3_column_blob(statement, 0));
        const int size = sqlite3_column_bytes(statement, 0);
        value = Bytes(data, data + size);
    }
    sqlite3_finalize(statement);
    return value;
}

std::string ProfileDb::text(const std::string& name) const
{
    const std::optional<Bytes> value = get(name);
    return value.has_value() ? std::string(value->begin(), value->end()) : std::string();
}

void ProfileDb::put(const std::string& name, const Bytes& value)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT INTO state (name, value) VALUES (?, ?)"
            " ON CONFLICT(name) DO UPDATE SET value = excluded.value",
            -1, &statement, nullptr)
        != SQLITE_OK) {
        throw std::runtime_error(std::string("profile database: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(statement, 1, name.c_str(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
    // A zero-length blob still needs a non-null pointer.
    const unsigned char empty = 0;
    sqlite3_bind_blob(statement, 2, value.empty() ? &empty : value.data(),
        static_cast<int>(value.size()), SQLITE_TRANSIENT);
    const int status = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (status != SQLITE_DONE) {
        throw std::runtime_error(std::string("profile database: ") + sqlite3_errmsg(db_));
    }
}

void ProfileDb::putText(const std::string& name, const std::string& value)
{
    put(name, Bytes(value.begin(), value.end()));
}

void ProfileDb::erase(const std::string& name)
{
    run(db_, "DELETE FROM state WHERE name = '" + quoted(name) + "'");
}

bool ProfileDb::has(const std::string& name) const
{
    return get(name).has_value();
}

void ProfileDb::rekey(const std::string& passphrase)
{
    run(db_, "PRAGMA rekey = '" + quoted(keyOrDefault(passphrase)) + "'");
}

}  // namespace bazarish::client
