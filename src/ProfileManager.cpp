// Bazarish project (c) 2026
#include "ProfileManager.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace bazarish::client {

namespace {

namespace fs = std::filesystem;

// Derives a safe directory id from a display name: lowercase, only
// [a-z0-9_-], other runs collapsed to a single '-'. Never empty.
std::string sanitizeId(const std::string& name)
{
    std::string id;
    bool lastDash = false;
    for (const char c : name) {
        char lowered = c;
        if (c >= 'A' && c <= 'Z') {
            lowered = static_cast<char>(c - 'A' + 'a');
        }
        const bool safe = (lowered >= 'a' && lowered <= 'z')
            || (lowered >= '0' && lowered <= '9') || lowered == '_';
        if (safe) {
            id.push_back(lowered);
            lastDash = false;
        } else if (!lastDash) {
            id.push_back('-');
            lastDash = true;
        }
    }
    while (!id.empty() && id.back() == '-') {
        id.pop_back();
    }
    std::size_t start = 0;
    while (start < id.size() && id[start] == '-') {
        ++start;
    }
    id = id.substr(start);
    if (id.empty()) {
        id = "profile";
    }
    return id;
}

std::string readFileText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

ProfileInfo readInfo(const std::string& id, const fs::path& dir)
{
    const nlohmann::json meta = nlohmann::json::parse(readFileText(dir / "meta.json"));
    ProfileInfo info;
    info.id = id;
    info.dir = dir;
    info.name = meta.value("name", std::string{});
    if (info.name.empty()) {
        info.name = id;
    }
    info.fingerprint = meta.value("fingerprint", std::string{});
    info.encrypted = meta.value("encrypted", false);
    info.connected
        = !meta.at("endpoint").value("facades", nlohmann::json::array()).empty();
    return info;
}

}  // namespace

fs::path ProfileManager::defaultRoot()
{
    if (const char* const xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return fs::path(xdg) / "bazarish" / "profiles";
    }
    const char* const home = std::getenv("HOME");
    const fs::path base = home != nullptr ? fs::path(home) : fs::current_path();
    return base / ".local" / "share" / "bazarish" / "profiles";
}

ProfileManager::ProfileManager(fs::path root)
    : root_(std::move(root))
{
    fs::create_directories(root_);
}

fs::path ProfileManager::dirFor(const std::string& id) const
{
    return root_ / id;
}

bool ProfileManager::exists(const std::string& id) const
{
    return fs::exists(dirFor(id) / "meta.json");
}

std::vector<ProfileInfo> ProfileManager::list() const
{
    std::vector<ProfileInfo> profiles;
    if (!fs::exists(root_)) {
        return profiles;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(root_)) {
        if (!entry.is_directory()) {
            continue;
        }
        const fs::path meta = entry.path() / "meta.json";
        if (!fs::exists(meta)) {
            continue;
        }
        profiles.push_back(readInfo(entry.path().filename().string(), entry.path()));
    }
    return profiles;
}

ProfileInfo ProfileManager::create(const std::string& name, const std::string& passphrase)
{
    const std::string id = sanitizeId(name);
    if (exists(id)) {
        throw std::runtime_error("a profile with this name already exists");
    }
    Session::create(dirFor(id), passphrase, name);
    return readInfo(id, dirFor(id));
}

Session ProfileManager::open(const std::string& id, const std::string& passphrase) const
{
    return Session::open(dirFor(id), passphrase);
}

ProfileInfo ProfileManager::import(const std::string& name, const fs::path& bundleFile,
    const std::string& password, const std::string& atRestPassphrase)
{
    // The display name is restored from the bundle (importState writes the bundled
    // meta verbatim). An explicit name, when given, only chooses the on-disk id;
    // when omitted the id is derived from the restored name. So import into a temp
    // dir first, read the restored name, then move it into place under its final id.
    const fs::path tmp = root_ / ".import-tmp";
    fs::remove_all(tmp);
    Session::importState(bundleFile, tmp, password, atRestPassphrase);
    const std::string restoredName = readInfo(std::string{}, tmp).name;
    const std::string id = sanitizeId(name.empty() ? restoredName : name);
    if (exists(id)) {
        fs::remove_all(tmp);
        throw std::runtime_error("a profile with this name already exists");
    }
    fs::rename(tmp, dirFor(id));
    return readInfo(id, dirFor(id));
}

void ProfileManager::remove(const std::string& id)
{
    fs::remove_all(dirFor(id));
}

}  // namespace bazarish::client
