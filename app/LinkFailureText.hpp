// Bazarish project (c) 2026
#pragma once

#include <FailureCause.hpp>
#include <OutboundCourier.hpp>

#include <bazarish/Sam.hpp>

#include <QString>

#include <exception>

namespace bazarish::app {

enum class FarSide { eServer, eSite, eRegistry, eContactServer, eContact, eDevice };

// failure: a client::LinkFailure or kLinkRebuilding.
QString linkFailureText(int failure, FarSide side);
QString localFailureText(client::LocalFailure failure);
QString causeText(const client::FailureCause& cause, FarSide side);
QString failureText(const std::exception& error, FarSide side);
QString notStoredText(client::NotStored reason);
QString samProbeText(sam::ProbeFailure failure);

}  // namespace bazarish::app
