// Bazarish project (c) 2026
#include "OperationLog.hpp"

#include <QDateTime>
#include <QTimer>

namespace bazarish::app {

namespace {
constexpr int kDoneRowKeptMs = 3500;
constexpr int kFailedRowKeptMs = 6000;
}  // namespace

int OperationLog::indexOf(const QString& id) const
{
    for (int i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id == id) {
            return i;
        }
    }
    return -1;
}

void OperationLog::begin(const QString& id, const QString& kind, const QString& title,
    const QString& status, const QString& peer, const QString& cancelId)
{
    OperationRow row;
    row.id = id;
    row.kind = kind;
    row.title = title;
    row.status = status;
    row.state = eOpRunning;
    row.startedAt = QDateTime::currentMSecsSinceEpoch();
    row.peer = peer;
    row.cancelId = cancelId;
    const int at = indexOf(id);
    if (at < 0) {
        rows_.prepend(row);
    } else {
        rows_[at] = row;
    }
    emit changed(row);
}

void OperationLog::update(
    const QString& id, const QString& status, const QString& detail, const double progress)
{
    const int at = indexOf(id);
    if (at < 0) {
        return;
    }
    OperationRow& row = rows_[at];
    row.status = status;
    row.detail = detail;
    if (progress >= 0.0) {
        row.progress = progress;
    }
    row.state = eOpRunning;
    emit changed(row);
}

void OperationLog::setCancelId(const QString& id, const QString& cancelId)
{
    const int at = indexOf(id);
    if (at < 0 || rows_[at].cancelId == cancelId) {
        return;
    }
    rows_[at].cancelId = cancelId;
    emit changed(rows_[at]);
}

void OperationLog::finish(const QString& id, const bool ok, const QString& finalStatus)
{
    const int at = indexOf(id);
    if (at < 0) {
        return;
    }
    OperationRow& row = rows_[at];
    row.status = finalStatus;
    row.detail.clear();
    row.state = ok ? eOpDone : eOpFailed;
    emit changed(row);
    QTimer::singleShot(ok ? kDoneRowKeptMs : kFailedRowKeptMs, this, [this, id]() {
        const int gone = indexOf(id);
        if (gone < 0) {
            return;
        }
        rows_.removeAt(gone);
        emit removed(id);
    });
}

}  // namespace bazarish::app
