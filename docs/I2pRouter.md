# The embedded I2P router

This client runs its own I2P router in-process (libi2pd, no system daemon, no
SAM). Its window - Settings -> I2P router - shows what the engine is doing and
holds the three settings below.

## Tunnel length

One choice for every destination this application builds: minimal (1 hop each
way), middle (1 hop with a variance of 1, so I2P adds zero or one), or maximum (3
hops, the depth I2P itself defaults to). The default is minimal, and the page
says plainly what one hop does and does not hide.

Call media always runs minimal whatever this says: three hops each way puts
audible delay into a live call.

## libi2pd logging

Off by default and fully suppressed - the engine does not even format the
messages. A debugging aid; turning it on puts libi2pd's own log lines into this
application's log.

## SOCKS proxy for the clearnet side

Off by default. Host and port only. What it covers is everything the router does
**outside** I2P: its connections to other routers, and the network database it
bootstraps from. It hides that traffic from the local network and the operator of
the link, and shows all of it to the proxy instead.

The limits are worth knowing before turning it on:

- **SOCKS5 only, and no credentials.** libi2pd's SOCKS client offers no
  authentication method other than "none", for router connections and for the
  bootstrap alike. A proxy that demands a username and password will refuse it.
  (Its HTTP-proxy path does support Basic auth, but that path cannot carry the
  datagram transport at all, so this client does not offer it.)
- **The datagram transport (SSU2) is switched off while a proxy is set.** Its
  packets can only travel through SOCKS5's UDP ASSOCIATE, which many proxies -
  Tor among them - do not implement, and which libi2pd will only attempt against
  a literal address. Whatever the proxy will not carry, the transport sends
  around it, which is the one outcome a proxy must not have. The router runs on
  its other transport (NTCP2) alone; it is slower to find peers and nothing else.
- **The router stops publishing an address for itself.** With a proxy configured
  libi2pd advertises no NTCP2 address, so the router is outbound-only. For a
  client that is the normal state anyway.
- **A change applies when the router restarts.** The transports read the setting
  as they come up, so saving asks whether to restart the router now (a minute or
  two of rebuilding tunnels) or to leave it for the next start. The row under the
  fields shows what the engine is running with, which is how a saved-but-not-yet-
  applied setting is visible.
- **The application's own clearnet request is not proxied.** The one request this
  client makes outside I2P by itself - fetching a network database from a server's
  clearnet address, once, before it has any I2P transport - goes through the
  application's HTTP stack, which has no proxy support. Only the router's traffic
  takes the proxy.

Stored in `<accounts>/.i2p-proxy` as `host:port`; absent means no proxy.

## What a proxy does not do

It moves the question of who sees this router's clearnet traffic from the local
network to the proxy operator, who then sees all of it: the addresses of the I2P
peers it talks to and the server it bootstrapped from. It is a way to keep I2P
traffic off a network that would notice it, not a way to be anonymous towards the
proxy.
