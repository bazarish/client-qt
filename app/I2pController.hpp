// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <filesystem>
#include <QString>

namespace bazarish::app {

// QML-facing view of the process-global embedded I2P router: the tunnel length
// setting, an optional libi2pd log switch, read-only diagnostics (netDb size,
// floodfills, our tunnels) and the list of active direct transport connections.
// The router is shared by every account and is not optional - it is the transport
// this client speaks over, so there is no off. The client never relays transit
// traffic (the router runs notransit), so no transit-tunnel count is reported.

// Slider positions for the tunnel privacy profile, lowest hop count first.
inline constexpr int kMinimalPrivacyLevel = 0;
inline constexpr int kMiddlePrivacyLevel = 1;
inline constexpr int kMaxPrivacyLevel = 2;

class I2pController : public QObject {
    Q_OBJECT
    // libi2pd's own logging. OFF by default (fully suppressed); a debugging aid.
    Q_PROPERTY(bool loggingEnabled READ loggingEnabled WRITE setLoggingEnabled NOTIFY loggingChanged)
    // Tunnel hop length for every destination this app builds: 0 minimal, 1
    // middle, 2 maximum. Calls always run minimal whatever this says.
    Q_PROPERTY(int privacyLevel READ privacyLevel WRITE setPrivacyLevel NOTIFY privacyLevelChanged)
    // Whether the embedded router is currently running (it starts at launch when
    // enabled and stops when disabled); the counts below are meaningful only then.
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(int knownRouters READ knownRouters NOTIFY statusChanged)
    // How many routers the netDb has to hold before the engine is worth starting,
    // so the status page can say what a stopped router is waiting for.
    Q_PROPERTY(int minKnownRouters READ minKnownRouters CONSTANT)
    Q_PROPERTY(int floodfills READ floodfills NOTIFY statusChanged)
    Q_PROPERTY(int inboundTunnels READ inboundTunnels NOTIFY statusChanged)
    Q_PROPERTY(int outboundTunnels READ outboundTunnels NOTIFY statusChanged)
    // Active direct transport connections, one display string each (e.g.
    // "NTCP2 · out · abcd1234"), for a scrollable list.
    Q_PROPERTY(QStringList transports READ transports NOTIFY statusChanged)
    // The destinations this router operates right now: one map per row with
    // "label", "host", "state" and "tunnels". Tunnel counts are per destination,
    // unlike the router-wide counts above.
    Q_PROPERTY(QVariantList destinations READ destinations NOTIFY statusChanged)
    // The SOCKS5 proxy the router's clearnet side goes through - what is saved,
    // which is not always what is in force (a change waits for a restart).
    // Empty host = no proxy, which is the default.
    Q_PROPERTY(QString proxyHost READ proxyHost NOTIFY proxyChanged)
    Q_PROPERTY(int proxyPort READ proxyPort NOTIFY proxyChanged)
    // The transport: an external router over SAM, or the engine in this process.
    // Settled at the first start by looking for a router, and changed only with
    // an application restart - the embedded engine cannot be started twice in one
    // process, so the two cannot swap places while it runs.
    Q_PROPERTY(bool samEnabled READ samEnabled NOTIFY samChanged)
    Q_PROPERTY(QString samHost READ samHost NOTIFY samChanged)
    Q_PROPERTY(int samPort READ samPort NOTIFY samChanged)
    // What the engine itself says about it, for the status page. Meaningful only
    // while the router runs, and shown only when a proxy is configured at all.
    Q_PROPERTY(QString proxyNtcp2 READ proxyNtcp2 NOTIFY statusChanged)
    // The private gateway: a host that runs an I2P router so this device does
    // not have to. Asked once, before anything starts a router; changeable at
    // any time afterwards from the settings page.
    Q_PROPERTY(bool gatewayAsked READ gatewayAsked NOTIFY gatewayChanged)
    Q_PROPERTY(bool gatewayEnabled READ gatewayEnabled NOTIFY gatewayChanged)
    Q_PROPERTY(QString gatewayAddress READ gatewayAddress NOTIFY gatewayChanged)
    Q_PROPERTY(bool gatewayChecking READ gatewayChecking NOTIFY gatewayChanged)
    // The host a gateway is reached at, without the scheme or the secret path:
    // what a status line can show without giving the way in away.
    Q_PROPERTY(QString gatewayHost READ gatewayHost NOTIFY gatewayChanged)
    // Which of the three carries the traffic: "embedded", "sam" or "gateway".
    // One choice, so the settings page can bind to it instead of reconciling
    // two flags of its own.
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
    // Saves the proxy (an empty host clears it). restartNow also stops and starts
    // the router's network, which is what puts it in force; without it the saved
    // setting waits for the next start.
    Q_INVOKABLE void saveProxy(const QString& host, int port, bool restartNow);
    bool samEnabled() const { return samEnabled_; }
    QString samHost() const { return samHost_; }
    int samPort() const { return samPort_; }
    // Stores the transport for the next start. Nothing changes in this process:
    // the caller restarts the application, which is the only way to swap engines.
    Q_INVOKABLE void saveSam(bool enabled, const QString& host, int port);
    // Whether a router answers at that address right now, so the page can say so
    // before the user commits to a restart.
    Q_INVOKABLE bool samReachable(const QString& host, int port) const;
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
    // The choice in force is the one this process started with: the engine is
    // settled once, at start-up, and cannot be swapped under a running account.
    // True from the moment the chosen one differs, and the window then says so
    // and stops taking anything else.
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY transportChanged)
    bool restartNeeded() const { return transport() != transportAtStart_; }
    // Checks the address off the GUI thread and, if it answers, stores it with
    // the key it presented. An address that does not answer is not stored:
    // there is nothing useful to do with one, and saving it would only move the
    // failure to somewhere the user cannot see it. Answers with one of the two
    // signals below.
    Q_INVOKABLE void checkAndSaveGateway(const QString& address);
    // The user chose not to use one. Recorded, so the question is not asked
    // again, and the embedded router or SAM takes over as it always did.
    Q_INVOKABLE void skipGateway();
    // Stops using a gateway that was set, without un-asking the question.
    Q_INVOKABLE void clearGateway();
    // Puts the traffic on the engine in this process. Takes hold at the next
    // start, as every transport choice does.
    Q_INVOKABLE void useEmbedded();

    // Re-reads the router diagnostics (a no-op when it is not running). Cheap;
    // the status window calls it on a timer while open.
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
    // Why it was refused, in a sentence for a person.
    void gatewayRefused(const QString& reason);

private:
    // Brings the shared router into line with the enable flag off the GUI thread:
    // starts it (warming the netDb even with no active session) when enabled, stops
    // its network when disabled.
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
    bool samEnabled_ = false;
    // What this process actually runs on, taken once at start-up: the engine is
    // settled then and cannot be swapped under a running account.
    QString transportAtStart_;
    QString samHost_;
    int samPort_ = 0;
    int floodfills_ = 0;
    int inboundTunnels_ = 0;
    int outboundTunnels_ = 0;
    QStringList transports_;
    QVariantList destinations_;
};

}  // namespace bazarish::app
