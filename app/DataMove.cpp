// Bazarish project (c) 2026
#include "DataMove.hpp"

#include <bazarish/PathText.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>

namespace bazarish::app {

namespace {

namespace fs = std::filesystem;

constexpr const char* kAccountsDir = "accounts";
constexpr const char* kRouterDir = "i2p";
constexpr char kAccountIdEnd = '.';

struct Unit {
    fs::path fromDir;
    fs::path toDir;
    std::vector<fs::path> names;
    bool router = false;
};

struct Transfer {
    fs::path from;
    fs::path to;
    bool renamed = false;
};

bool present(const fs::path& path)
{
    return fs::symlink_status(path).type() != fs::file_type::not_found;
}

bool isDirectory(const fs::path& path)
{
    return fs::is_directory(fs::symlink_status(path));
}

bool isEmptyDirectory(const fs::path& path)
{
    return isDirectory(path) && fs::is_empty(path);
}

std::vector<fs::path> namesIn(const fs::path& dir)
{
    std::vector<fs::path> names;
    if (!isDirectory(dir)) {
        return names;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        names.push_back(entry.path().filename());
    }
    std::sort(names.begin(), names.end());
    return names;
}

// "<id>.db", "<id>.db-wal" and "<id>.key" share the id before the first dot.
std::string groupOf(const fs::path& name)
{
    const std::string text = utf8OfPath(name);
    const std::string id = text.substr(0, text.find(kAccountIdEnd));
    return id.empty() ? text : id;
}

bool isAccountGroup(const std::string& group)
{
    return group.find(kAccountIdEnd) == std::string::npos;
}

void planAccounts(const fs::path& fromDir, const fs::path& toDir, std::vector<Unit>& units,
    DataMoveResult& result)
{
    std::set<std::string> taken;
    for (const fs::path& name : namesIn(toDir)) {
        taken.insert(groupOf(name));
    }
    std::map<std::string, std::vector<fs::path>> groups;
    for (const fs::path& name : namesIn(fromDir)) {
        groups[groupOf(name)].push_back(name);
    }
    for (const auto& [group, names] : groups) {
        if (!taken.contains(group)) {
            units.push_back(Unit{fromDir, toDir, names});
            continue;
        }
        if (isAccountGroup(group)) {
            result.keptAccounts.push_back(group);
            continue;
        }
        for (const fs::path& name : names) {
            result.keptEntries.push_back(fromDir / name);
        }
    }
}

std::vector<Unit> plan(const fs::path& from, const fs::path& to, DataMoveResult& result)
{
    std::vector<Unit> units;
    for (const fs::path& name : namesIn(from)) {
        const fs::path source = from / name;
        const fs::path target = to / name;
        const bool router = name == kRouterDir;
        if (name == kAccountsDir && isDirectory(source)) {
            planAccounts(source, target, units, result);
        } else if (!present(target)) {
            units.push_back(Unit{from, to, {name}, router});
        } else if (isEmptyDirectory(target) && isDirectory(source)) {
            units.push_back(Unit{source, target, namesIn(source), router});
        } else {
            result.keptEntries.push_back(source);
        }
    }
    std::stable_partition(
        units.begin(), units.end(), [](const Unit& unit) { return !unit.router; });
    return units;
}

bool within(const fs::path& inner, const fs::path& outer)
{
    const fs::path relative
        = fs::weakly_canonical(inner).lexically_relative(fs::weakly_canonical(outer));
    return !relative.empty() && *relative.begin() != "..";
}

class Move {
public:
    void createDirectories(const fs::path& dir)
    {
        std::vector<fs::path> missing;
        for (fs::path step = dir; !step.empty() && !present(step); step = step.parent_path()) {
            missing.push_back(step);
        }
        for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
            if (!fs::create_directory(*it)) {
                throw fs::filesystem_error("directory appeared during the move", *it,
                    std::make_error_code(std::errc::file_exists));
            }
            createdDirs_.push_back(*it);
        }
    }

    void transfer(const fs::path& from, const fs::path& to)
    {
        if (present(to)) {
            throw fs::filesystem_error("already present", from, to,
                std::make_error_code(std::errc::file_exists));
        }
        std::error_code renameError;
        fs::rename(from, to, renameError);
        if (!renameError) {
            transfers_.push_back(Transfer{from, to, true});
            return;
        }
        transfers_.push_back(Transfer{from, to, false});
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks);
    }

    std::string undo()
    {
        std::string failures;
        for (auto it = transfers_.rbegin(); it != transfers_.rend(); ++it) {
            std::error_code error;
            if (it->renamed) {
                fs::rename(it->to, it->from, error);
            } else {
                fs::remove_all(it->to, error);
            }
            if (error) {
                failures += "; " + utf8OfPath(it->to) + ": " + error.message();
            }
        }
        for (auto it = createdDirs_.rbegin(); it != createdDirs_.rend(); ++it) {
            std::error_code error;
            fs::remove(*it, error);
            if (error) {
                failures += "; " + utf8OfPath(*it) + ": " + error.message();
            }
        }
        return failures;
    }

    void clearSources(const std::vector<Unit>& units, const fs::path& root,
        DataMoveResult& result) const
    {
        for (const Transfer& done : transfers_) {
            if (done.renamed) {
                continue;
            }
            std::error_code error;
            fs::remove_all(done.from, error);
            if (error) {
                result.leftBehind.push_back(done.from);
            }
        }
        std::set<fs::path> dirs;
        for (const Unit& unit : units) {
            if (unit.fromDir != root) {
                dirs.insert(unit.fromDir);
            }
        }
        for (const fs::path& dir : dirs) {
            removeIfEmpty(dir, result);
        }
        removeIfEmpty(root, result);
    }

private:
    static void removeIfEmpty(const fs::path& dir, DataMoveResult& result)
    {
        std::error_code error;
        const bool empty = fs::is_empty(dir, error);
        if (!error && empty) {
            fs::remove(dir, error);
        }
        if (error) {
            result.leftBehind.push_back(dir);
        }
    }

    std::vector<fs::path> createdDirs_;
    std::vector<Transfer> transfers_;
};

}  // namespace

DataMoveResult moveDataRoot(const fs::path& from, const fs::path& to, const DataMoveSteps& steps)
{
    if (within(to, from) || within(from, to)) {
        throw std::runtime_error("one data folder lies inside the other");
    }
    DataMoveResult result;
    const std::vector<Unit> units = plan(from, to, result);
    Move move;
    try {
        for (const Unit& unit : units) {
            if (unit.router) {
                steps.beforeRouterMoves();
            }
            move.createDirectories(unit.toDir);
            for (const fs::path& name : unit.names) {
                move.transfer(unit.fromDir / name, unit.toDir / name);
            }
        }
        steps.commit();
    } catch (const std::exception& error) {
        const std::string undoFailures = move.undo();
        if (undoFailures.empty()) {
            throw;
        }
        throw std::runtime_error(std::string(error.what()) + "; not undone" + undoFailures);
    }
    if (!units.empty()) {
        move.clearSources(units, from, result);
    }
    return result;
}

}  // namespace bazarish::app
