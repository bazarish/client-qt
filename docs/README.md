# Client documentation

## Scope

This directory documents the Bazarish desktop client: its settings, the values it
applies where the protocol specifies none, and the limits of its implementation.

The protocol - wire formats, delivery states, admission rules, and the
obligations of any implementation - is specified in the `docs-main` repository
and is common to all clients. Where the two overlap, `docs-main` defines the
requirement and this directory records how this client meets it. For example, the
delivery states and when a delivery is attempted again are specified by the
protocol; how this client shows a message that waits is not.

## Contents

| Document | Subject |
|---|---|
| [Installation.md](Installation.md) | What the installation keeps outside any account: the root on each platform, portable mode, the settings document and its size limit, and the command line |
| [I2pRouter.md](I2pRouter.md) | How I2P is carried: the three transports and how one is chosen, the TLS of the gateway link, tunnel length, engine logging, and the SOCKS5 proxy applied to the embedded engine's clearnet traffic |
| [Sending.md](Sending.md) | Outbound delivery: the outbox, what opens the way to a recipient again, reported states, what admits a message |
| [Receiving.md](Receiving.md) | Incoming mail: the one request that hears about it, what the connection strip says and what a lost network does, what a pass takes, what the upkeep tick carries, the service rows a conversation holds, and what orders a conversation |
| [ConnectionLog.md](ConnectionLog.md) | The connection log window: what it records, what it deliberately leaves out, and how much it keeps |
| [AccountStorage.md](AccountStorage.md) | The account database: SQLCipher version and profile, the key sidecar, what a failed open reports, what a conversation weighs on this device and what trimming it removes |
| [Devices.md](Devices.md) | The devices of one account: how a second one starts, what it takes from the backup and from its siblings, the device lists, revoking one, the mail-key warning and what a secondary device does not offer |
| [SavedAndBlocking.md](SavedAndBlocking.md) | The saved-messages chat, blocking a correspondent, and the per-contact notification and call switches |
| [Calls.md](Calls.md) | Calls: what carries the media, how a datagram reaches a b33, the lane kept for real time, and why a call leaves nothing in the chat |
| [Sounds.md](Sounds.md) | The notification sound and the ringtone: where a recording of the user's own goes, what the ringtone has to be, and what the call window's pulse follows |
| [Processes.md](Processes.md) | The core that keeps the accounts and the window that shows them: what each holds, the frames between them, and what a window attaching is told |
| [BackgroundActivity.md](BackgroundActivity.md) | The activity panel: what is reported there while it runs, and what a row says |
| [Formatting.md](Formatting.md) | Message formatting: the markers a body may carry, what a press on one does, and where a body is shown without them |
| [Groups.md](Groups.md) | Groups and channels: what makes a chat one, the author line and letter circle, reaction counts, the admin actions and the descriptor plate |

## Not covered here

Implemented, with no page here: voice messages, pictures, avatars, reactions,
bots and inline keyboards, aliases as the client services them,
sign-in-with-key, invites and QR, several accounts at once, and conversation
search with its paging.

The repository `README.md` names the dependencies and the build.
