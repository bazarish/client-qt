// Bazarish project (c) 2026
#pragma once

#include <QString>

#include <filesystem>

namespace bazarish::app {

// Through UTF-16 both ways: a std::string between a QString and a path is ANSI on Windows.
inline std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdU16String());
}

inline QString pathText(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

}  // namespace bazarish::app
