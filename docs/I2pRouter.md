# Embedded I2P router

## Scope

The desktop client operates an I2P router in its own process (libi2pd; no system
daemon). This document specifies the router settings exposed to the user, their
storage, and the constraints that apply to each.

The transport is a facade over two engines. The desktop uses the in-process one,
for the reasons this document sets out: the network database comes from the
user's own server rather than a public reseed host, and the clearnet side can be
put behind a proxy. A router outside the process, reached over SAM v3 on
loopback, is the other engine; it exists for a service running many accounts,
where one router serves every process. An external router answers none of the
diagnostics below, and neither the proxy setting nor the private reseed applies
to it - both belong to whoever operates it.

The settings are presented in Settings -> I2P router, which also reports the
engine's live diagnostics (network database size, floodfills, tunnel counts,
local destinations, active transport sessions).

## Settings

| Setting | Key in `settings.json` | Default |
|---|---|---|
| Tunnel length | `i2p.tunnelLength` | 0 (minimal) |
| Engine logging | `i2p.logging` | `false` |
| SOCKS5 proxy | `i2p.proxy.host`, `i2p.proxy.port` | empty, 0 (no proxy) |

All application settings are held in one JSON document, `settings.json`, at the
root of the installation; see the repository `README.md`. The accounts directory
and the router's state directory (`i2p/`) are beside it. An empty proxy host or a
port of zero is stored as "no proxy": neither half is retained on its own.

## Tunnel length

Applies to every destination the application builds:

| Level | Hops per direction | Variance |
|---|---|---|
| Minimal | 1 | 0 |
| Middle | 1 | 1 (I2P adds zero or one hop) |
| Maximum | 3 | 0 |

Call media is exempt and always uses the minimal profile: additional hops
introduce audible latency in a live call.

A change applies to destinations created after it. Destinations already in use
are rebuilt by the application so that the previous profile is not retained.

## Engine logging

Disabled by default; the engine's log messages are suppressed before formatting.
When enabled, libi2pd log records are emitted through the application log. The
setting is intended for diagnostics.

## SOCKS5 proxy

Disabled by default. Configuration consists of a host (name or address) and a
port. When set, the following router traffic is directed through the proxy:

- connections to other I2P routers (NTCP2 transport);
- retrieval of the initial network database from libi2pd's built-in reseed hosts.

### Constraints

1. **SOCKS5 only; no authentication.** The libi2pd SOCKS client offers the "no
   authentication" method exclusively, both for transport connections and for
   reseed retrieval. A proxy requiring credentials will reject the connection.
   The engine's HTTP-proxy path supports Basic authentication but cannot carry
   the datagram transport, and is therefore not offered by this client.
2. **The datagram transport (SSU2) is disabled while a proxy is configured.**
   SSU2 traffic can traverse a SOCKS5 proxy only through the UDP ASSOCIATE
   command, which many proxies do not implement and which libi2pd attempts only
   against a literal address. If the proxy does not carry it, the engine sends
   these datagrams directly, bypassing the proxy. The transport is therefore
   disabled unconditionally, and the router operates on NTCP2 alone.
3. **The router publishes no transport address.** With a proxy configured,
   libi2pd does not publish an NTCP2 address, so the router accepts no inbound
   connections. This is the expected condition for a client installation.
4. **Application requests are not proxied.** The single clearnet request the
   application performs itself - retrieving a network database from a server's
   clearnet address before an I2P transport exists - uses the application HTTP
   stack, which does not implement proxy support. Only router traffic is subject
   to the proxy setting.

### Application of a change

The transports read the proxy configuration when they start. Saving the setting
therefore offers two outcomes:

| Choice | Effect |
|---|---|
| Save and restart | The setting is stored and the router's network is stopped and started, which applies it immediately. Tunnels are rebuilt, which typically takes one to two minutes. |
| Save only | The setting is stored and applied the next time the router starts. |

The value reported under the input fields is read back from the engine, so a
setting that has been stored but not yet applied is distinguishable from one in
force.

### Residual exposure

A proxy relocates the observation of this router's clearnet traffic from the
local network and the access provider to the proxy operator, who then observes
all of it, including the addresses of the I2P peers contacted and the host used
for the initial network database. It is a means of keeping I2P traffic off a
network that would otherwise observe it, not a means of anonymity with respect to
the proxy.
