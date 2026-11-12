// Bazarish project (c) 2026
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bazarish::app {

struct DataMoveSteps {
    std::function<void()> beforeRouterMoves;
    std::function<void()> commit;
};

struct DataMoveResult {
    std::vector<std::string> keptAccounts;
    std::vector<std::filesystem::path> keptEntries;
    std::vector<std::filesystem::path> leftBehind;
};

// Throws after undoing every change it made when a transfer or commit fails.
DataMoveResult moveDataRoot(
    const std::filesystem::path& from, const std::filesystem::path& to, const DataMoveSteps& steps);

}  // namespace bazarish::app
