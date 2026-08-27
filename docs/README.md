# Client documentation

What is written here is what **this** client does: its settings, the policies it
chose where the protocol left a choice, and the limits of its own machinery.

The protocol itself - the wire formats, the delivery states, what a server is
allowed to know - lives in `docs-main` and is the same for every client. When the
two touch, this repository says "how", `docs-main` says "what": the delivery
states are the protocol's, the number of attempts before this client gives up is
not.

| Document | Subject |
|---|---|
| [I2pRouter.md](I2pRouter.md) | The embedded I2P router: tunnel length, the SOCKS proxy for its clearnet side, and what each costs |
| [Sending.md](Sending.md) | How this client carries its outgoing mail: attempts, what the ticks mean, resending |

The application itself (windows, background activity, packaging) is described in
the repository `README.md`.
