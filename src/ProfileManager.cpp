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

// The extension every profile file carries.
constexpr const char* kFileSuffix = ".db";

// A profile is stored under its own name, so the name has to survive as a file
// name. Only what a file system refuses is replaced - the reserved characters of
// Windows and macOS included, so a portable copy on a stick stays readable - and
// everything else, Unicode included, is kept as the user typed it.
std::string sanitizeFileName(const std::string& name)
{
    static const std::string reserved = "/\\:*?\"<>|";
    std::string out;
    for (const char c : name) {
        const bool control = static_cast<unsigned char>(c) < 0x20;
        out.push_back(control || reserved.find(c) != std::string::npos ? '_' : c);
    }
    // Leading dots hide the file; trailing dots and spaces are dropped by Windows.
    const std::size_t first = out.find_first_not_of('.');
    out = first == std::string::npos ? std::string() : out.substr(first);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

// What can be told about a profile without opening it fully. A profile with a
// passphrase gives up nothing until it is unlocked - not its name, not its
// fingerprint - which is the point of keeping everything in one keyed file. It
// is listed by its directory id and marked locked.
ProfileInfo readInfo(const std::string& id, const fs::path& file, const std::string& passphrase = {})
{
    ProfileInfo info;
    info.id = id;
    info.file = file;
    info.name = id;
    // One open, not two: unlocking a profile database runs its key derivation,
    // which is deliberately expensive.
    std::unique_ptr<ProfileDb> db;
    try {
        db = std::make_unique<ProfileDb>(file, passphrase);
    } catch (const std::exception&) {
        info.encrypted = true;  // the key does not open it: locked
        return info;
    }
    const nlohmann::json meta = nlohmann::json::parse(db->text("meta"));
    // For a profile in place the name and the file name are the same string; an
    // imported bundle is read before it has a file name of its own, and there the
    // name inside is all there is.
    info.name = meta.value("name", id);
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

fs::path ProfileManager::fileFor(const std::string& id) const
{
    return root_ / (sanitizeFileName(id) + kFileSuffix);
}

bool ProfileManager::exists(const std::string& id) const
{
    return fs::exists(fileFor(id));
}

std::vector<ProfileInfo> ProfileManager::list() const
{
    std::vector<ProfileInfo> profiles;
    if (!fs::exists(root_)) {
        return profiles;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(root_)) {
        if (!entry.is_regular_file() || entry.path().extension() != kFileSuffix) {
            continue;
        }
        profiles.push_back(readInfo(entry.path().stem().string(), entry.path()));
    }
    return profiles;
}

ProfileInfo ProfileManager::create(const std::string& name, const std::string& passphrase)
{
    const std::string id = sanitizeFileName(name);
    if (id.empty()) {
        throw std::runtime_error("a profile needs a name");
    }
    if (exists(id)) {
        throw std::runtime_error("a profile with this name already exists");
    }
    Session::create(fileFor(id), passphrase, id);
    return readInfo(id, fileFor(id), passphrase);
}

Session ProfileManager::open(const std::string& id, const std::string& passphrase) const
{
    return Session::open(fileFor(id), passphrase);
}

ProfileInfo ProfileManager::import(const std::string& name, const fs::path& bundleFile,
    const std::string& password, const std::string& atRestPassphrase)
{
    // The display name is restored from the bundle (importProfile writes the bundled
    // meta verbatim). An explicit name, when given, only chooses the on-disk id;
    // when omitted the id is derived from the restored name. So import into a temp
    // dir first, read the restored name, then move it into place under its final id.
    const fs::path tmp = root_ / ".import-tmp.db";
    fs::remove(tmp);
    Session::importProfile(bundleFile, tmp, password, atRestPassphrase);
    const std::string restoredName = readInfo(std::string{}, tmp, atRestPassphrase).name;
    const std::string id = sanitizeFileName(name.empty() ? restoredName : name);
    if (id.empty() || exists(id)) {
        fs::remove(tmp);
        throw std::runtime_error(id.empty() ? "a profile needs a name"
                                            : "a profile with this name already exists");
    }
    fs::rename(tmp, fileFor(id));
    return readInfo(id, fileFor(id), atRestPassphrase);
}

void ProfileManager::remove(const std::string& id)
{
    fs::remove(fileFor(id));
}

}  // namespace bazarish::client
