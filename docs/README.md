# Client documentation

## Scope

This directory documents the Bazarish desktop client: its settings, the values it
applies where the protocol specifies none, and the limits of its implementation.

The protocol - wire formats, delivery states, admission rules, and the
obligations of any implementation - is specified in the `docs-main` repository
and is common to all clients. Where the two overlap, `docs-main` defines the
requirement and this directory records how this client meets it. For example, the
delivery states are specified by the protocol; the number of delivery attempts
this client performs before reporting failure is not.

## Contents

| Document | Subject |
|---|---|
| [I2pRouter.md](I2pRouter.md) | The embedded I2P router: tunnel length, engine logging, and the SOCKS5 proxy applied to its clearnet traffic |
| [Sending.md](Sending.md) | Outbound delivery: attempt schedule, reported states, failure handling, delivery-token accounting |
| [AccountStorage.md](AccountStorage.md) | The account database: SQLCipher version and profile, the key sidecar, what a failed open reports |
| [Formatting.md](Formatting.md) | Message formatting: the markers a body may carry, what a press on one does, and where a body is shown without them |

Application structure, build instructions and packaging are described in the
repository `README.md`.
