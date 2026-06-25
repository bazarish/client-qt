// Bazarish project (c) 2026
#pragma once

#include "Session.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace bazarish::client {

// Summary of a profile on disk, readable without the passphrase: the fields
// here are public (a fingerprint and a label), so the picker can list profiles
// before any of them is unlocked.
struct ProfileInfo {
    // Stable identifier = the on-disk directory name (sanitized).
    std::string id;
    // Human display label set at creation (may fall back to the id).
    std::string name;
    std::filesystem::path dir;
    std::string fingerprint;
    bool encrypted = false;
    // A serving server is configured (the endpoint host is set).
    bool connected = false;
};

// Manages the set of local profiles under a root directory (each profile is its
// own directory). Enumerates, creates, opens and removes them.
class ProfileManager {
public:
    // Default location: $XDG_DATA_HOME/bazarish/profiles (or ~/.local/share).
    static std::filesystem::path defaultRoot();

    explicit ProfileManager(std::filesystem::path root);

    std::vector<ProfileInfo> list() const;
    bool exists(const std::string& id) const;
    std::filesystem::path dirFor(const std::string& id) const;

    // Creates a new profile from a display name (the id is derived from it).
    // Throws if a profile with that id already exists.
    ProfileInfo create(const std::string& name, const std::string& passphrase = {});
    // Opens an existing profile; the passphrase is required when encrypted.
    Session open(const std::string& id, const std::string& passphrase = {}) const;
    // Imports an exported bundle as a new profile under the given name.
    ProfileInfo import(const std::string& name, const std::filesystem::path& bundleFile,
        const std::string& password, const std::string& atRestPassphrase = {});
    void remove(const std::string& id);

private:
    std::filesystem::path root_;
};

}  // namespace bazarish::client
