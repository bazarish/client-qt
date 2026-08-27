// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <filesystem>

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
    // What the engine itself says about it, for the status page. Meaningful only
    // while the router runs, and shown only when a proxy is configured at all.
    Q_PROPERTY(QString proxyNtcp2 READ proxyNtcp2 NOTIFY statusChanged)
    Q_PROPERTY(QString proxySsu2 READ proxySsu2 NOTIFY statusChanged)
    Q_PROPERTY(bool proxySsu2Enabled READ proxySsu2Enabled NOTIFY statusChanged)
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
    QString proxySsu2() const { return proxySsu2_; }
    bool proxySsu2Enabled() const { return proxySsu2Enabled_; }
    // Saves the proxy (an empty host clears it). restartNow also stops and starts
    // the router's network, which is what puts it in force; without it the saved
    // setting waits for the next start.
    Q_INVOKABLE void saveProxy(const QString& host, int port, bool restartNow);
    int floodfills() const { return floodfills_; }
    int inboundTunnels() const { return inboundTunnels_; }
    int outboundTunnels() const { return outboundTunnels_; }
    QStringList transports() const { return transports_; }
    QVariantList destinations() const { return destinations_; }

    // Re-reads the router diagnostics (a no-op when it is not running). Cheap;
    // the status window calls it on a timer while open.
    Q_INVOKABLE void refresh();

signals:
    void loggingChanged();
    void privacyLevelChanged();
    void statusChanged();
    void proxyChanged();

private:
    std::filesystem::path loggingPath() const;
    std::filesystem::path privacyPath() const;
    std::filesystem::path proxyPath() const;
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
    QString proxySsu2_;
    bool proxySsu2Enabled_ = true;
    int floodfills_ = 0;
    int inboundTunnels_ = 0;
    int outboundTunnels_ = 0;
    QStringList transports_;
    QVariantList destinations_;
};

}  // namespace bazarish::app
