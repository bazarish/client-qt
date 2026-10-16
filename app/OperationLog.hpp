// Bazarish project (c) 2026
#pragma once

#include "AccountEvents.hpp"

#include <QObject>
#include <QString>
#include <QVector>

namespace bazarish::app {

class OperationLog : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    void begin(const QString& id, const QString& kind, const QString& title,
        const QString& status, const QString& peer = {}, const QString& cancelId = {});
    void update(const QString& id, const QString& status, const QString& detail = {},
        double progress = kProgressUnknown);
    void setCancelId(const QString& id, const QString& cancelId);
    void finish(const QString& id, bool ok, const QString& finalStatus);
    bool has(const QString& id) const { return indexOf(id) >= 0; }
    QVector<OperationRow> rows() const { return rows_; }

signals:
    void changed(const OperationRow& row);
    void removed(const QString& id);

private:
    int indexOf(const QString& id) const;

    QVector<OperationRow> rows_;
};

}  // namespace bazarish::app
