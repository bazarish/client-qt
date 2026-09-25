// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <QString>

namespace bazarish::app {

// The few values both halves of a session need: the worker stamps and measures
// what it sends, and the window draws the same values back.

// Unix milliseconds: the message display and ordering clock (sentAt is in ms).
qint64 nowMillis();

// A compact human size ("1.4 MB"), for transfer progress.
QString humanBytes(qint64 bytes);

// A voice message's drawn shape, one hex digit a bar (a level is exactly a
// digit). Empty when the audio will not unpack, so the bubble draws no waveform
// rather than a made-up one.
QString waveformHex(const Bytes& opus);

// The protocol id a new outgoing message is named by. Drawn here so a send and
// the record it leaves behind carry the same one.
QString newE2eId();

// The activity row the alias errand runs under. Named in one place because the
// row is opened where the errand starts and closed where its answer arrives.
inline const QString kAliasOperationId = QStringLiteral("alias");

}  // namespace bazarish::app
