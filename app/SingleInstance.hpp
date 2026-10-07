// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QLocalServer;
class QLockFile;

namespace bazarish::app {

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QString folder, QObject* parent = nullptr);
    ~SingleInstance() override;

    bool claim();
    bool handOver(const QString& link);

signals:
    void showRequested();
    void linkRequested(const QString& link);

private:
    QString socketName() const;

    const QString folder_;
    std::unique_ptr<QLockFile> lock_;
    std::unique_ptr<QLocalServer> server_;
};

}  // namespace bazarish::app
