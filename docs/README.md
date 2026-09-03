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
| [ConnectionLog.md](ConnectionLog.md) | The connection log window: what it records, what it deliberately leaves out, and how much it keeps |
| [AccountStorage.md](AccountStorage.md) | The account database: SQLCipher version and profile, the key sidecar, what a failed open reports |
| [Devices.md](Devices.md) | The devices of one account: how a second one starts, what it takes from the backup and from its siblings, and what forgetting one does |
| [SavedAndBlocking.md](SavedAndBlocking.md) | The saved-messages chat, blocking a correspondent, and the per-contact notification and call switches |
| [Calls.md](Calls.md) | Calls: what carries the media, the router patch a b33 datagram needs, the lane kept for real time, and why a call leaves nothing in the chat |
| [Sounds.md](Sounds.md) | The notification sound and the ringtone: where a recording of the user's own goes, what the ringtone has to be, and what the call window's pulse follows |
| [Formatting.md](Formatting.md) | Message formatting: the markers a body may carry, what a press on one does, and where a body is shown without them |

Application structure, build instructions and packaging are described in the
repository `README.md`.
