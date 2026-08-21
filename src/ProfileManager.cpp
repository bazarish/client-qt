// Bazarish project (c) 2026
#include "ProfileManager.hpp"

#include "ProfileDb.hpp"

#include <memory>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <stdexcept>

namespace bazarish::client {

namespace {

namespace fs = std::filesystem;

// How many profiles may share a sanitised id before creation gives up. A wall,
// not a limit anybody should reach.
constexpr int kMaxIdSuffix = 99;

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

// What can be told about a profile without opening it fully. A profile with a
// passphrase gives up nothing until it is unlocked - not its name, not its
// fingerprint - which is the point of keeping everything in one keyed file. It
// is listed by its directory id and marked locked.
ProfileInfo readInfo(const std::string& id, const fs::path& dir, const std::string& passphrase = {})
{
    ProfileInfo info;
    info.id = id;
    info.dir = dir;
    info.name = id;
    // One open, not two: unlocking a profile database runs its key derivation,
    // which is deliberately expensive.
    std::unique_ptr<ProfileDb> db;
    try {
        db = std::make_unique<ProfileDb>(dir, passphrase);
    } catch (const std::exception&) {
        info.encrypted = true;  // the key does not open it: locked
        return info;
    }
    const nlohmann::json meta = nlohmann::json::parse(db->text("meta"));
    info.name = meta.value("name", std::string{});
    if (info.name.empty()) {
        info.name = id;
    }
    info.fingerprint = meta.value("fingerprint", std::string{});
    info.encrypted = meta.value("encrypted", false);
    info.connected = !meta.at("endpoint").value("facades", nlohmann::json::array()).empty();
    return info;
}

}  // namespace

fs::path ProfileManager::globalRoot()
{
    if (const char* const xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return fs::path(xdg) / "bazarish";
    }
    const char* const home = std::getenv("HOME");
    const fs::path base = home != nullptr ? fs::path(home) : fs::current_path();
    return base / ".local" / "share" / "bazarish";
}

namespace {

// The directory the running executable lives in. Read from the process itself
// rather than argv[0], which a caller can set to anything.
fs::path executableDir()
{
    std::error_code error;
    const fs::path self = fs::read_symlink("/proc/self/exe", error);
    return error ? fs::current_path() : self.parent_path();
}

}  // namespace

fs::path ProfileManager::portableMarker()
{
    return executableDir() / ".bazarish.portable";
}

fs::path ProfileManager::portableRoot()
{
    return executableDir() / "bazarish_data";
}

bool ProfileManager::portable()
{
    std::error_code ignored;
    return fs::exists(portableMarker(), ignored);
}

fs::path ProfileManager::dataRoot()
{
    return portable() ? portableRoot() : globalRoot();
}

fs::path ProfileManager::defaultRoot()
{
    return dataRoot() / "profiles";
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
    return fs::exists(dirFor(id) / ProfileDb::kFileName);
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
        if (!fs::exists(entry.path() / ProfileDb::kFileName)) {
            continue;
        }
        profiles.push_back(readInfo(entry.path().filename().string(), entry.path()));
    }
    return profiles;
}

ProfileInfo ProfileManager::create(const std::string& name, const std::string& passphrase)
{
    std::string id = sanitizeId(name);
    if (exists(id)) {
        // Two different names can sanitise to the same id - a name with no ASCII
        // in it at all sanitises to nothing and falls back to "profile" - so only
        // a genuine repeat of the display name is a duplicate; the rest take the
        // next free id.
        if (readInfo(id, dirFor(id)).name == name) {
            throw std::runtime_error("a profile with this name already exists");
        }
        std::string free;
        for (int suffix = 2; suffix <= kMaxIdSuffix; ++suffix) {
            const std::string candidate = id + "-" + std::to_string(suffix);
            if (!exists(candidate)) {
                free = candidate;
                break;
            }
        }
        if (free.empty()) {
            throw std::runtime_error("too many profiles with this name");
        }
        id = free;
    }
    Session::create(dirFor(id), passphrase, name);
    return readInfo(id, dirFor(id), passphrase);
}

Session ProfileManager::open(const std::string& id, const std::string& passphrase) const
{
    return Session::open(dirFor(id), passphrase);
}

ProfileInfo ProfileManager::import(const std::string& name, const fs::path& bundleFile,
    const std::string& password, const std::string& atRestPassphrase)
{
    // The display name is restored from the bundle (importProfile writes the bundled
    // meta verbatim). An explicit name, when given, only chooses the on-disk id;
    // when omitted the id is derived from the restored name. So import into a temp
    // dir first, read the restored name, then move it into place under its final id.
    const fs::path tmp = root_ / ".import-tmp";
    fs::remove_all(tmp);
    Session::importProfile(bundleFile, tmp, password, atRestPassphrase);
    const std::string restoredName = readInfo(std::string{}, tmp, atRestPassphrase).name;
    const std::string id = sanitizeId(name.empty() ? restoredName : name);
    if (exists(id)) {
        fs::remove_all(tmp);
        throw std::runtime_error("a profile with this name already exists");
    }
    fs::rename(tmp, dirFor(id));
    return readInfo(id, dirFor(id), atRestPassphrase);
}

void ProfileManager::remove(const std::string& id)
{
    fs::remove_all(dirFor(id));
}

}  // namespace bazarish::client
