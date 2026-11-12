// Bazarish project (c) 2026
#include "DataMove.hpp"
#include "TestUtil.hpp"

#include <bazarish/PathText.hpp>

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;
using bazarish::app::DataMoveResult;
using bazarish::app::DataMoveSteps;
using bazarish::app::moveDataRoot;

using Tree = std::map<std::string, std::string>;

constexpr const char* kDirectoryMark = "<dir>";
constexpr fs::perms kReadOnlyDirectory = fs::perms::owner_read | fs::perms::owner_exec;
constexpr fs::perms kNoAccess = fs::perms::none;
constexpr fs::perms kUsualDirectory = fs::perms::owner_all;
constexpr fs::perms kUsualFile = fs::perms::owner_read | fs::perms::owner_write;

void writeFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    CHECK(out.is_open());
    out << text;
    out.close();
    CHECK(!out.fail());
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    CHECK(in.is_open());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Tree snapshot(const fs::path& root)
{
    Tree tree;
    if (!fs::exists(root)) {
        return tree;
    }
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
        const std::string name = bazarish::utf8OfPath(entry.path().lexically_relative(root));
        tree[name] = entry.is_directory() ? std::string(kDirectoryMark) : readFile(entry.path());
    }
    return tree;
}

Tree merged(const Tree& base, const Tree& added)
{
    Tree out = base;
    for (const auto& [name, content] : added) {
        CHECK(out.emplace(name, content).second);
    }
    return out;
}

Tree without(const Tree& tree, const std::string& prefix)
{
    Tree out;
    for (const auto& [name, content] : tree) {
        if (!name.starts_with(prefix)) {
            out.emplace(name, content);
        }
    }
    return out;
}

void fillSource(const fs::path& from)
{
    writeFile(from / "accounts" / "a1.db", "a1 database");
    writeFile(from / "accounts" / "a1.key", "a1 key");
    writeFile(from / "accounts" / "a1.db-wal", "a1 wal");
    writeFile(from / "accounts" / "b2.db", "b2 database");
    writeFile(from / "accounts" / "b2.key", "b2 key");
    writeFile(from / "settings.json", "{\"language\":\"ru\"}");
    writeFile(from / "ringtone.wav", "source ringtone");
    writeFile(from / "i2p" / "router.keys", "source router keys");
    writeFile(from / "i2p" / "netDb" / "r1.dat", "source router info");
}

struct Calls {
    int router = 0;
    int commit = 0;
};

DataMoveSteps countingSteps(Calls& calls)
{
    DataMoveSteps steps;
    steps.beforeRouterMoves = [&calls]() { ++calls.router; };
    steps.commit = [&calls]() { ++calls.commit; };
    return steps;
}

struct Roots {
    fs::path base;
    fs::path from;
    fs::path to;
};

Roots freshRoots(const std::string& tag)
{
    const fs::path base = testutil::freshDir("datamove-" + tag);
    return Roots{base, base / "from", base / "to"};
}

void emptyDestinationTakesEverything()
{
    const Roots roots = freshRoots("empty");
    fillSource(roots.from);
    const Tree source = snapshot(roots.from);
    Calls calls;
    const DataMoveResult result = moveDataRoot(roots.from, roots.to, countingSteps(calls));
    CHECK(snapshot(roots.to) == source);
    CHECK(!fs::exists(roots.from));
    CHECK(calls.router == 1);
    CHECK(calls.commit == 1);
    CHECK(result.keptAccounts.empty());
    CHECK(result.keptEntries.empty());
    CHECK(result.leftBehind.empty());
    fs::remove_all(roots.base);
}

void sameAccountIsSkippedWhole()
{
    const Roots roots = freshRoots("same-account");
    fillSource(roots.from);
    writeFile(roots.to / "accounts" / "a1.db", "destination a1 database");
    const Tree source = snapshot(roots.from);
    const Tree destination = snapshot(roots.to);
    Calls calls;
    const DataMoveResult result = moveDataRoot(roots.from, roots.to, countingSteps(calls));
    CHECK(result.keptAccounts == std::vector<std::string>{"a1"});
    CHECK(result.keptEntries.empty());
    CHECK(readFile(roots.to / "accounts" / "a1.db") == "destination a1 database");
    CHECK(!fs::exists(roots.to / "accounts" / "a1.key"));
    CHECK(!fs::exists(roots.to / "accounts" / "a1.db-wal"));
    Tree moved = without(source, "accounts/a1.");
    CHECK(moved.erase("accounts") == 1);
    CHECK(snapshot(roots.to) == merged(destination, moved));
    const Tree left = snapshot(roots.from);
    CHECK(left == (Tree{{"accounts", kDirectoryMark}, {"accounts/a1.db", "a1 database"},
        {"accounts/a1.key", "a1 key"}, {"accounts/a1.db-wal", "a1 wal"}}));
    CHECK(calls.commit == 1);
    fs::remove_all(roots.base);
}

void settingsAndRouterAtDestinationStay()
{
    const Roots roots = freshRoots("kept");
    fillSource(roots.from);
    writeFile(roots.to / "settings.json", "{\"language\":\"es\"}");
    writeFile(roots.to / "i2p" / "router.keys", "destination router keys");
    const Tree source = snapshot(roots.from);
    const Tree destination = snapshot(roots.to);
    Calls calls;
    const DataMoveResult result = moveDataRoot(roots.from, roots.to, countingSteps(calls));
    CHECK(calls.router == 0);
    CHECK(result.keptEntries.size() == 2);
    CHECK(std::count(result.keptEntries.cbegin(), result.keptEntries.cend(),
              roots.from / "settings.json") == 1);
    CHECK(std::count(result.keptEntries.cbegin(), result.keptEntries.cend(),
              roots.from / "i2p") == 1);
    const Tree after = snapshot(roots.to);
    for (const auto& [name, content] : destination) {
        CHECK(after.at(name) == content);
    }
    const Tree stayed = snapshot(roots.from);
    CHECK(stayed == without(without(source, "accounts"), "ringtone.wav"));
    CHECK(readFile(roots.to / "ringtone.wav") == "source ringtone");
    CHECK(readFile(roots.to / "accounts" / "b2.key") == "b2 key");
    fs::remove_all(roots.base);
}

void newAccountJoinsExistingOnes()
{
    const Roots roots = freshRoots("alongside");
    writeFile(roots.from / "accounts" / "d4.db", "d4 database");
    writeFile(roots.from / "accounts" / "d4.key", "d4 key");
    writeFile(roots.to / "accounts" / "c3.db", "c3 database");
    writeFile(roots.to / "accounts" / "c3.key", "c3 key");
    writeFile(roots.to / "settings.json", "{}");
    const Tree destination = snapshot(roots.to);
    Calls calls;
    const DataMoveResult result = moveDataRoot(roots.from, roots.to, countingSteps(calls));
    CHECK(result.keptAccounts.empty());
    CHECK(snapshot(roots.to)
        == merged(destination, Tree{{"accounts/d4.db", "d4 database"},
            {"accounts/d4.key", "d4 key"}}));
    CHECK(!fs::exists(roots.from));
    fs::remove_all(roots.base);
}

void emptyRouterDirectoryIsFilled()
{
    const Roots roots = freshRoots("empty-router");
    fillSource(roots.from);
    fs::create_directories(roots.to / "i2p");
    const Tree source = snapshot(roots.from);
    Calls calls;
    const DataMoveResult result = moveDataRoot(roots.from, roots.to, countingSteps(calls));
    CHECK(calls.router == 1);
    CHECK(snapshot(roots.to) == source);
    CHECK(!fs::exists(roots.from));
    CHECK(result.leftBehind.empty());
    fs::remove_all(roots.base);
}

void failedCopyLeavesBothSidesAsTheyWere()
{
    if (::geteuid() == 0) {
        std::fprintf(stderr, "failed-copy case skipped: permissions do not bind root\n");
        return;
    }
    const Roots roots = freshRoots("copy-fails");
    fillSource(roots.from);
    writeFile(roots.from / "i2p" / "sealed.dat", "unreadable");
    writeFile(roots.to / "accounts" / "c3.db", "c3 database");
    const Tree source = snapshot(roots.from);
    const Tree destination = snapshot(roots.to);
    fs::permissions(roots.from / "i2p" / "sealed.dat", kNoAccess);
    fs::permissions(roots.from / "i2p", kReadOnlyDirectory);
    Calls calls;
    CHECK_THROWS(moveDataRoot(roots.from, roots.to, countingSteps(calls)));
    CHECK(calls.router == 1);
    CHECK(calls.commit == 0);
    fs::permissions(roots.from / "i2p", kUsualDirectory);
    fs::permissions(roots.from / "i2p" / "sealed.dat", kUsualFile);
    CHECK(snapshot(roots.to) == destination);
    CHECK(snapshot(roots.from) == source);
    fs::remove_all(roots.base);
}

void failedCommitLeavesBothSidesAsTheyWere()
{
    const Roots roots = freshRoots("commit-fails");
    fillSource(roots.from);
    writeFile(roots.to / "accounts" / "c3.db", "c3 database");
    writeFile(roots.to / "settings.json", "{}");
    const Tree source = snapshot(roots.from);
    const Tree destination = snapshot(roots.to);
    Calls calls;
    DataMoveSteps steps = countingSteps(calls);
    steps.commit = []() { throw std::runtime_error("marker not written"); };
    CHECK_THROWS(moveDataRoot(roots.from, roots.to, steps));
    CHECK(calls.router == 1);
    CHECK(snapshot(roots.to) == destination);
    CHECK(snapshot(roots.from) == source);

    const fs::path fresh = roots.base / "fresh";
    CHECK_THROWS(moveDataRoot(roots.from, fresh, steps));
    CHECK(!fs::exists(fresh));
    CHECK(snapshot(roots.from) == source);
    fs::remove_all(roots.base);
}

void emptySourceOnlyCommits()
{
    const Roots roots = freshRoots("empty-source");
    Calls calls;
    const DataMoveResult absent = moveDataRoot(roots.from, roots.to, countingSteps(calls));
    CHECK(calls.commit == 1);
    CHECK(!fs::exists(roots.to));
    fs::create_directories(roots.from);
    const DataMoveResult empty = moveDataRoot(roots.from, roots.to, countingSteps(calls));
    CHECK(calls.commit == 2);
    CHECK(calls.router == 0);
    CHECK(!fs::exists(roots.to));
    CHECK(absent.leftBehind.empty() && empty.leftBehind.empty());
    fs::remove_all(roots.base);
}

void nestedRootsAreRefused()
{
    const Roots roots = freshRoots("nested");
    fillSource(roots.from);
    const Tree source = snapshot(roots.from);
    Calls calls;
    CHECK_THROWS(moveDataRoot(roots.from, roots.from / "inner", countingSteps(calls)));
    CHECK_THROWS(moveDataRoot(roots.from / "accounts", roots.from, countingSteps(calls)));
    CHECK(calls.commit == 0);
    CHECK(snapshot(roots.from) == source);
    fs::remove_all(roots.base);
}

}  // namespace

int main()
{
    emptyDestinationTakesEverything();
    sameAccountIsSkippedWhole();
    settingsAndRouterAtDestinationStay();
    newAccountJoinsExistingOnes();
    emptyRouterDirectoryIsFilled();
    failedCopyLeavesBothSidesAsTheyWere();
    failedCommitLeavesBothSidesAsTheyWere();
    emptySourceOnlyCommits();
    nestedRootsAreRefused();
    return 0;
}
