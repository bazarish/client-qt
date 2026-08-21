// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <filesystem>
#include <optional>
#include <string>

// The SQLCipher connection handle, opaque here.
struct sqlite3;

namespace bazarish::client {

// A profile's storage: one encrypted SQLite (SQLCipher) file holding everything
// the profile is - keys, metadata, contacts, blobs and the message transcript.
// Nothing about a profile is written beside it, so a profile is one file that is
// unreadable without its key.
//
// This class owns the small named values; the transcript owns its own tables on
// the same file through its own connection.
class ProfileDb {
public:
    // The file name inside a profile directory.
    static constexpr const char* kFileName = "profile.db";
    // What a profile with no passphrase is keyed with: it keeps one code path and
    // stops the file from being readable by accident. It is not protection from
    // anyone who has read this source.
    static constexpr const char* kDefaultKey = "bazarish";

    // Opens (creating it when absent) the database in `profileDir`, keyed with
    // `passphrase` or kDefaultKey when that is empty. Throws when the key does not
    // open an existing file - a wrong passphrase must not read as an empty profile.
    ProfileDb(const std::filesystem::path& profileDir, const std::string& passphrase);
    ~ProfileDb();

    ProfileDb(const ProfileDb&) = delete;
    ProfileDb& operator=(const ProfileDb&) = delete;

    // Whether the key opens this database. Used to tell a locked profile from an
    // unlocked one without throwing.
    static bool opens(const std::filesystem::path& profileDir, const std::string& passphrase);

    std::optional<Bytes> get(const std::string& name) const;
    std::string text(const std::string& name) const;  // empty when absent
    void put(const std::string& name, const Bytes& value);
    void putText(const std::string& name, const std::string& value);
    void erase(const std::string& name);
    bool has(const std::string& name) const;

    // Re-keys the whole file in place (changing or setting the passphrase).
    void rekey(const std::string& passphrase);

private:
    sqlite3* db_ = nullptr;
};

}  // namespace bazarish::client
