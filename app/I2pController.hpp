// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <filesystem>
#include <QString>

namespace bazarish::app {

inline constexpr int kMinimalPrivacyLevel = 0;
inline constexpr int kMiddlePrivacyLevel = 1;
inline constexpr int kMaxPrivacyLevel = 2;

class I2pController : public QObject {
    Q_OBJECT
    // libi2pd's own logging. OFF by default (fully suppressed); a debugging aid.
    Q_PROPERTY(bool loggingEnabled READ loggingEnabled WRITE setLoggingEnabled NOTIFY loggingChanged)
    Q_PROPERTY(int privacyLevel READ privacyLevel WRITE setPrivacyLevel NOTIFY privacyLevelChanged)
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(int knownRouters READ knownRouters NOTIFY statusChanged)
    Q_PROPERTY(int minKnownRouters READ minKnownRouters CONSTANT)
    Q_PROPERTY(int floodfills READ floodfills NOTIFY statusChanged)
    Q_PROPERTY(int inboundTunnels READ inboundTunnels NOTIFY statusChanged)
    Q_PROPERTY(int outboundTunnels READ outboundTunnels NOTIFY statusChanged)
    Q_PROPERTY(QStringList transports READ transports NOTIFY statusChanged)
    Q_PROPERTY(QVariantList destinations READ destinations NOTIFY statusChanged)
    Q_PROPERTY(QString proxyHost READ proxyHost NOTIFY proxyChanged)
    Q_PROPERTY(int proxyPort READ proxyPort NOTIFY proxyChanged)
    Q_PROPERTY(bool samEnabled READ samEnabled NOTIFY samChanged)
    Q_PROPERTY(bool samChecking READ samChecking NOTIFY samChanged)
    Q_PROPERTY(QString samHost READ samHost NOTIFY samChanged)
    Q_PROPERTY(int samPort READ samPort NOTIFY samChanged)
    Q_PROPERTY(QString proxyNtcp2 READ proxyNtcp2 NOTIFY statusChanged)
    Q_PROPERTY(bool gatewayAsked READ gatewayAsked NOTIFY gatewayChanged)
    Q_PROPERTY(bool gatewayEnabled READ gatewayEnabled NOTIFY gatewayChanged)
    Q_PROPERTY(QString gatewayAddress READ gatewayAddress NOTIFY gatewayChanged)
    Q_PROPERTY(bool gatewayChecking READ gatewayChecking NOTIFY gatewayChanged)
    Q_PROPERTY(QString gatewayHost READ gatewayHost NOTIFY gatewayChanged)
    Q_PROPERTY(QString transport READ transport NOTIFY transportChanged)
public:
    explicit I2pController(QObject* parent = nullptr);

    bool loggingEnabled() const { return loggingEnabled_; }
    int privacyLevel() const { return privacyLevel_; }
    void setPrivacyLevel(int level);
    void setLoggingEnabled(bool on);
    bool running() const { return running_; }
    bool ready() const { return ready_; }
    int knownRouters() const { return knownRouters_; }
    int minKnownRouters() const;
    QString proxyHost() const { return proxyHost_; }
    int proxyPort() const { return proxyPort_; }
    QString proxyNtcp2() const { return proxyNtcp2_; }
    Q_INVOKABLE void saveProxy(const QString& host, int port, bool restartNow);
    bool samEnabled() const { return samEnabled_; }
    bool samChecking() const { return samChecking_; }
    QString samHost() const { return samHost_; }
    int samPort() const { return samPort_; }
    Q_INVOKABLE void checkAndSaveSam(const QString& host, int port);
    Q_INVOKABLE void useSam(bool on);
    Q_INVOKABLE void useGateway(bool on);
    int floodfills() const { return floodfills_; }
    int inboundTunnels() const { return inboundTunnels_; }
    int outboundTunnels() const { return outboundTunnels_; }
    QStringList transports() const { return transports_; }
    QVariantList destinations() const { return destinations_; }

    bool gatewayAsked() const { return gatewayAsked_; }
    bool gatewayEnabled() const { return gatewayEnabled_; }
    QString gatewayAddress() const { return gatewayAddress_; }
    bool gatewayChecking() const { return gatewayChecking_; }
    QString gatewayHost() const;
    QString transport() const;
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY transportChanged)
    bool restartNeeded() const { return transport() != transportAtStart_; }
    Q_INVOKABLE void checkAndSaveGateway(const QString& address);
    Q_INVOKABLE void skipGateway();
    Q_INVOKABLE void clearGateway();
    Q_INVOKABLE void useEmbedded();

    Q_INVOKABLE void refresh();

signals:
    void loggingChanged();
    void privacyLevelChanged();
    void statusChanged();
    void proxyChanged();
    void samChanged();
    void gatewayChanged();
    void transportChanged();
    void gatewaySaved();
    void gatewayRefused(const QString& reason);
    void samSaved();
    void samRefused(const QString& reason);

private:
    void reconcileRouter();

    bool loggingEnabled_ = false;
    int privacyLevel_ = kMinimalPrivacyLevel;
    bool running_ = false;
    bool ready_ = false;
    int knownRouters_ = 0;
    QString proxyHost_;
    int proxyPort_ = 0;
    QString proxyNtcp2_;
    bool gatewayAsked_ = false;
    bool gatewayEnabled_ = false;
    QString gatewayAddress_;
    bool gatewayChecking_ = false;
    bool samChecking_ = false;
    bool samEnabled_ = false;
    QString transportAtStart_;
    void noteChoiceInForce();
    QString samHost_;
    int samPort_ = 0;
    int floodfills_ = 0;
    int inboundTunnels_ = 0;
    int outboundTunnels_ = 0;
    QStringList transports_;
    QVariantList destinations_;
};

}  // namespace bazarish::app
