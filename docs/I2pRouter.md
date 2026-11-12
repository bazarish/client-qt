# Embedded I2P router

## Scope

The desktop client operates an I2P router in its own process (libi2pd). This document specifies the router settings exposed to the user, their
storage, and the constraints that apply to each.

### Choosing the transport

The in-process engine carries the traffic unless the user says otherwise. The
choice sits at the foot of Global settings as three tabs - **Embedded**, **SAM
API** and **Private gateway** - each with a switch of its own; turning one on
turns the others off, because only one of them can carry the traffic.

- **Embedded** is the in-process engine, and what a desktop uses.
- **SAM API** is a router outside the process, reached over SAM v3 on loopback
  (127.0.0.1:7656 by default). It exists for a service running many accounts,
  where one router serves every process.
- **Private gateway** is a host that runs a router for this device, reached over
  a WebSocket. Its address carries a key, which is what admits this device; the
  address is checked before it is saved, and a gateway that does not answer is
  not stored.

Turning a switch on asks the address whether anything is there and reports the
answer in the same dialog that says the application has to be restarted. The
change takes effect at the next start: the in-process engine can be started only
once in a process, and the engines cannot swap places while one is running - the
dialog offers to close the application there and then. The router window then
reports whichever transport is in force.

On a first run, before any engine is up, the application asks once whether a
private gateway should be used. Skipping the question leaves the embedded engine
in place and it is not asked again.

While an external router carries the traffic, what belongs to it rather than to
this application is not shown: the network-database counters, the transport
peers and the clearnet proxy. The list of local destinations still applies. Under
SAM the tunnel length is sent with every destination this client opens; a gateway
sets the tunnels itself, the same for all its clients, so under a gateway the
router window carries no tunnel length at all and the client keeps no warm
reserve - the gateway keeps one for everybody. A destination the gateway joined
without I2P (a direct route to its operator's server, or two of its clients
meeting inside it) reads "gateway routing" instead of a LeaseSet count.

The transport is a facade over three engines. The desktop uses the in-process one,
for the reasons this document sets out: the network database comes from the
user's own server, and the clearnet side can be
put behind a proxy. A router outside the process, reached over SAM v3 on
loopback, is the other engine; it exists for a service running many accounts,
where one router serves every process. An external router answers none of the
diagnostics below, and neither the proxy setting nor the private reseed applies
to it - both belong to whoever operates it.

The proxy belongs to the in-process engine, so it is set on that engine's own
tab in Global settings and can be set before the engine runs. The router window
carries the tunnel length and reports the engine's live diagnostics (network
database size, floodfills, tunnel counts, local destinations, active transport
sessions); it sets nothing else.

### The gateway link's TLS

The connection to a private gateway is TLS shaped as Chrome's, so that a watcher
of the device's link sees what a browser on that host would show. It is built on
BoringSSL at the commit Chrome 157.0.8103.78 is built with
(`5b83e5d1abf0ae0033ae52ab579d906b67633b2c`), compiled with the client under the
symbol prefix `BZB` and used for this link alone; everything else in the client
uses OpenSSL.

- **ClientHello.** Chrome's settings: GREASE (ciphers, extensions, groups,
  versions, signature algorithms), a new extension order on every connection, ECH
  GREASE, brotli certificate compression, the groups X25519MLKEM768, X25519,
  P-256 and P-384 with key shares for the first two, Chrome's cipher list and
  signature algorithms, SCT and OCSP requests, and Chrome's Trust Anchor IDs.
- **Two profiles, as Chrome has.** A WebSocket offers ALPN `http/1.1` alone; a
  decoy connection offers `h2,http/1.1` and ALPS for `h2`. The front must answer
  `http/1.1`: a decoy connection on which it chooses `h2` is closed as a failure.
- **Resumption.** Sessions are kept per host and port for an hour, two TLS 1.3
  tickets at most, each used once, as Chrome keeps them.
- **No close_notify.** A connection ends at the TCP layer, as Chrome ends one.
- **Requests.** The WebSocket upgrade and the decoy requests carry Chrome's
  headers in Chrome's order, with the User-Agent and client hints of Chrome 157
  on the platform the client runs on (Linux, Android, Windows or macOS). A decoy
  page is asked as a navigation, a decoy resource as an image.
- **Certificate.** Not checked, as before: the channel inside the socket proves
  the gateway. The front's certificate has to be one Chrome's signature
  algorithms cover (ECDSA or RSA); an Ed25519 certificate fails the handshake.

Measured against Chrome 157.0.8103.78 on Linux (2026-11-12): JA4
`t13d1516h1_8daaf6152771_5b37b36a67c0` for a fresh WebSocket,
`t13d1517h2_8daaf6152771_cb7bf5808d99` for a page, and
`t13d1517h1_8daaf6152771_c4a234726a38` for a WebSocket that resumes a page's
session; the client gives the same three. `TestChromeTls` checks them.

On Windows the link is built over OpenSSL (`BAZARISH_GATEWAY_TLS_OPENSSL`, README)
and shows OpenSSL's ClientHello.

## Bootstrap

A router with no netDb builds no tunnel, so the first thing it needs is a slice
of the network. This client does none of that work itself: the reseed addresses
from the server descriptor - https **base** URLs, the shape every I2P reseed has -
are handed to the engine (`reseed.urls`) before it starts, and it appends
`i2pseeds.su3`, fetches, unpacks and loads them the way it would any reseed. The
addresses are refused unless they are `https`, and a missing trailing slash is
put back, because the engine concatenates rather than joins.

Three consequences worth stating:

- **The engine does the fetch**, so the request carries the user agent every I2P
  router sends and follows redirects like every other router, and does not mark
  the client as Bazarish to the host or to anyone in the path.
- **The su3 signature is not checked** (`reseed.verify` is off, which is i2pd's
  default). A reseed an operator runs privately is signed by nobody a stock
  client trusts. Nothing is lost by it: every RouterInfo inside carries its own
  signature and is verified on load, so what a hostile archive can do is choose
  which real routers you meet first - the same power the addresses in the
  descriptor already have. What stands between the archive and the network is
  TLS, which is why the address must be https.
- **With no reseed address, or when none of them answered, the engine uses its
  own built-in hosts.** That is the only case in which the bootstrap reaches
  outside the network the user chose. i2pd reseeds only as its network database
  starts and gives up after 180 seconds for good, so a pass that found nothing
  restarts the router before the next one; the next pass comes with the next
  mail wait, 30 seconds later. Measured on a fresh data directory: ready in about 30 seconds
  with 80 routers from the built-in hosts, and about a minute with 98 from a
  named su3 URL.

## Settings

| Setting | Key in `settings.json` | Default |
|---|---|---|
| Tunnel length | `i2p.tunnelLength` | 0 (minimal) |
| Engine logging | `i2p.logging` | `false` |
| SOCKS5 proxy | `i2p.proxy.host`, `i2p.proxy.port` | empty, 0 (no proxy) |
| External router | `i2p.sam.enabled`, `i2p.sam.host`, `i2p.sam.port` | `false`, 127.0.0.1, 7656 |
| Private gateway | `i2p.gateway.enabled`, `i2p.gateway.address`, `i2p.gateway.pin` | `false`, empty, empty |
| Gateway question answered | `i2p.gateway.asked` | `false` |

The gateway address carries the key that admits this device, and it is kept in
`settings.json` as it was typed. The settings document is not encrypted - it is
installation-wide and is read before any account is unlocked - so anybody who
can read the file can read that key.

All application settings are held in one JSON document, `settings.json`, at the
root of the installation ([Installation.md](Installation.md)). The accounts directory
and the router's state directory (`i2p/`) are beside it. An empty proxy host or a
port of zero is stored as "no proxy": neither half is retained on its own.

## Tunnel length

Applies to every destination the application builds on its own engine or over
SAM; a gateway decides it for its clients:

| Level | Hops per direction | Variance |
|---|---|---|
| Minimal | 1 | 0 |
| Middle | 1 | 1 (I2P adds zero or one hop) |
| Maximum | 3 | 0 |

Call media is exempt and always uses the minimal profile: additional hops
introduce audible latency in a live call.

A change applies to destinations created after it. Destinations already in use
are rebuilt by the application so that the previous profile is not retained.

## Call media paths

A call's destination keeps six tunnels each way; other destinations keep three. Every 100 ms it
sends a path probe (`docs-main/api/Calls.md`) on one pairing of its own outbound
tunnel with one of the peer's inbound tunnels, every other probe on the pairing the
audio uses, and keeps a smoothed round trip per pairing: each answer moves it a
quarter of the way. Audio is pinned to the fastest pairing that has answered and
moves only to one at least 30 ms faster. A probe left unanswered for 2 s counts as
a 2 s round trip for a pairing that has answered before and as nothing for one
that never has; until one answers, the engine picks the path on its own.

The engine does this itself, so it holds on this machine's router and on a
gateway, which runs the same engine for its clients. Over SAM the six tunnels are
asked for too, but the router alone picks the path: no probe is sent or answered.

## Engine logging

Disabled by default; the engine's log messages are suppressed before formatting.
When enabled, libi2pd log records are emitted through the application log. The
setting is intended for diagnostics.

## SOCKS5 proxy

Disabled by default. Configuration consists of a host (name or address) and a
port. When set, the following router traffic is directed through the proxy:

- connections to other I2P routers (NTCP2 transport);
- retrieval of the initial network database, from the descriptor's reseed
  addresses or from libi2pd's built-in hosts.

### Constraints

1. **SOCKS5 only; no authentication.** The libi2pd SOCKS client offers the "no
   authentication" method exclusively, both for transport connections and for
   reseed retrieval. A proxy requiring credentials will reject the connection.
2. **The datagram transport (SSU2) is disabled while a proxy is configured.**
   SSU2 traffic can traverse a SOCKS5 proxy only through the UDP ASSOCIATE
   command, which many proxies do not implement and which libi2pd attempts only
   against a literal address. If the proxy does not carry it, the engine sends
   these datagrams directly, bypassing the proxy. The transport is therefore
   disabled unconditionally, and the router operates on NTCP2 alone.
3. **The router publishes no transport address.** With a proxy configured,
   libi2pd does not publish an NTCP2 address, so the router accepts no inbound
   connections. This is the expected condition for a client installation.
4. **Only router traffic is proxied.** The application makes no clearnet request
   of its own: the API is spoken over I2P, and the reseed is fetched by the
   engine. A facade reached over clearnet - on loopback, or under the developer
   flag - is not proxied.

### Application of a change

The transports read the proxy configuration when they start. **Save** first asks
the address for a SOCKS5 handshake without credentials; a refusal is reported
under the fields and nothing is stored. An accepted proxy is stored, as is an
empty host or port that removes one, and a running engine's network is stopped
and started at once.
Tunnels are rebuilt, which typically takes one to two minutes.

### Residual exposure

A proxy relocates the observation of this router's clearnet traffic from the
local network and the access provider to the proxy operator, who then observes
all of it, including the addresses of the I2P peers contacted and the host used
for the initial network database. It is a means of keeping I2P traffic off a
network that would otherwise observe it, not a means of anonymity with respect to
the proxy.
