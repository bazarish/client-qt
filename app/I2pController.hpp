// Bazarish project (c) 2026
#pragma once

#include <QObject>
#include <QStringList>

#include <filesystem>

namespace bazarish::app {

// QML-facing view of the process-global embedded I2P router: a persistent on/off
// setting, an optional libi2pd log switch, read-only diagnostics (netDb size,
// floodfills, our tunnels) and the list of active direct transport connections.
// The router is shared by every account; this object reports it and flips the
// transport's enable/logging flags. Turning I2P off persists across runs and is
// never re-enabled automatically: the network then runs on clearnet facades
// only. The client never relays transit traffic (the router runs notransit), so
// no transit-tunnel count is reported.
class I2pController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // libi2pd's own logging. OFF by default (fully suppressed); a debugging aid.
    Q_PROPERTY(bool loggingEnabled READ loggingEnabled WRITE setLoggingEnabled NOTIFY loggingChanged)
    // Whether the embedded router has been started (it warms up at launch when
    // enabled); the counts below are meaningful only while running.
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(int knownRouters READ knownRouters NOTIFY statusChanged)
    Q_PROPERTY(int floodfills READ floodfills NOTIFY statusChanged)
    Q_PROPERTY(int inboundTunnels READ inboundTunnels NOTIFY statusChanged)
    Q_PROPERTY(int outboundTunnels READ outboundTunnels NOTIFY statusChanged)
    // Active direct transport connections, one display string each (e.g.
    // "NTCP2 · out · abcd1234"), for a scrollable list.
    Q_PROPERTY(QStringList transports READ transports NOTIFY statusChanged)
public:
    explicit I2pController(QObject* parent = nullptr);

    bool enabled() const { return enabled_; }
    void setEnabled(bool on);
    bool loggingEnabled() const { return loggingEnabled_; }
    void setLoggingEnabled(bool on);
    bool running() const { return running_; }
    bool ready() const { return ready_; }
    int knownRouters() const { return knownRouters_; }
    int floodfills() const { return floodfills_; }
    int inboundTunnels() const { return inboundTunnels_; }
    int outboundTunnels() const { return outboundTunnels_; }
    QStringList transports() const { return transports_; }

    // Re-reads the router diagnostics (a no-op when it is not running). Cheap;
    // the status window calls it on a timer while open.
    Q_INVOKABLE void refresh();

signals:
    void enabledChanged();
    void loggingChanged();
    void statusChanged();

private:
    std::filesystem::path settingPath() const;
    std::filesystem::path loggingPath() const;
    // Starts the shared router off the GUI thread (idempotent) when enabled, so it
    // keeps warming the netDb even with no active session. No-op when disabled.
    void ensureRouterWarm();

    bool enabled_ = true;
    bool loggingEnabled_ = false;
    bool running_ = false;
    bool ready_ = false;
    int knownRouters_ = 0;
    int floodfills_ = 0;
    int inboundTunnels_ = 0;
    int outboundTunnels_ = 0;
    QStringList transports_;
};

}  // namespace bazarish::app
