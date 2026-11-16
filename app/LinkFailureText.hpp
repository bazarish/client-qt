// Bazarish project (c) 2026
#pragma once

#include <QString>

#include <exception>

namespace bazarish::app {

enum class FarSide { eServer, eSite, eRegistry };

// failure: a client::LinkFailure or kLinkRebuilding.
QString linkFailureText(int failure, FarSide side);
QString failureText(const std::exception& error, FarSide side);

}  // namespace bazarish::app
