// Bazarish project (c) 2026
#pragma once

#include "I2pEvents.hpp"

#include <QObject>
#include <QTimer>

namespace bazarish::app {

class Relay;

class I2pService : public QObject {
    Q_OBJECT
public:
    explicit I2pService(QObject* parent = nullptr);

    void attach(Relay& relay);
    void announce();

signals:
    void tunnelsNeedRebuild();

private:
    void setPrivacyLevel(int level);
    void setLoggingEnabled(bool on);
    void checkAndSaveProxy(const QString& host, int port);
    void saveProxy(const QString& host, int port);
    void checkAndSaveSam(const QString& host, int port);
    void useSam(bool on);
    void useGateway(bool on);
    void checkAndSaveGateway(const QString& address);
    void skipGateway();
    void clearGateway();
    void useEmbedded();
    void refresh();
    void noteChoiceInForce();
    QString transport() const;

    I2pCommands commands_;
    I2pEvents events_;
    QTimer rebuildDelay_;
    I2pState state_;
    QString transportAtStart_;
};

}  // namespace bazarish::app
