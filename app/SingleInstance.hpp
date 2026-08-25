// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QLocalServer;
class QLockFile;

namespace bazarish::app {

// One application per account folder. Two copies sharing one folder each hold
// their own view of the same database and each register as the same device, so a
// message is handed to whichever asks first and the other never learns of it -
// its open chat quietly stops showing what arrives in it.
// A second start hands over to the one already running instead: its window comes
// forward and the newcomer exits. To run two clients on one machine, give the
// second one a folder of its own with BAZARISH_ACCOUNTS_DIR.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QString folder, QObject* parent = nullptr);
    ~SingleInstance() override;

    // True when this process is the one holding the folder. False means another
    // is; call handOver() and leave.
    bool claim();
    // Asks the holder to show itself. False when nobody answered - a lock held by
    // a process that is no longer listening.
    bool handOver();

signals:
    // Another copy was started and handed over to this one.
    void showRequested();

private:
    QString socketName() const;

    const QString folder_;
    std::unique_ptr<QLockFile> lock_;
    std::unique_ptr<QLocalServer> server_;
};

}  // namespace bazarish::app
