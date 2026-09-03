# Bazarish - client-qt6

The desktop client for the [Bazarish](https://github.com/bazarish/docs-main)
messenger: a Qt6 Quick application over a client core library.

- **Core library** - `ApiClient` (hybrid-signed transport over the client API),
  `Client` (typed endpoint wrappers + sealed-envelope construction) and `Session`
  (stateful: identity, sealing key, contacts, one-time tokens, E2E encryption,
  mailbox sync, calls, file transfer). Shared with the other implementations
  through the `common` submodule.
- **Application** - `bazarish-app`, the QML front end over that `Session`.

Headless use is not this repository's job: a bot or a scripted account is
`client-daemon`, which runs the same core without an interface.

A picture and a voice message ride **inside** the message, so they arrive with
it: nothing is announced, nothing is fetched, and the mailbox holds them like any
other message even if the sender goes offline the moment after sending. A picture
is scaled and re-encoded first (long edge 1600, at most 256 KiB); a voice message
is Opus at the call format - 48 kHz mono, 20 ms frames, each length-prefixed -
which is a few hundred KiB for a minute or two. Both sit inside the protocol's
512 KiB message limit; a larger file is transferred directly between the two
clients instead ([docs-main `api/FileTransfer.md`](https://github.com/bazarish/docs-main)).

The sealed body of a message is **CBOR**, not JSON text. It is the same document
with the same field names, but binary values travel as themselves - which is what
lets a small attachment ride inside the message it belongs to instead of being
announced and fetched. Base64 would have cost a third of every such payload (1.33x
measured), while compressing it buys nothing: pictures and Opus audio are already
entropy-coded.

Messages are end-to-end encrypted to the peer's user sealing key. The **first**
contact request is encrypted too, to the sealing **prekey** in the peer's
user-signed contact card, which the client fetches over I2P from the destination
in the invite - there is no plaintext first message, and no server is asked who
is being added. The server stores only opaque ciphertext.

Account keys live in one encrypted database (SQLCipher 4) whose key is held in a
small sealed file beside it, and the whole account can be **exported** into a
single password-encrypted bundle (CMS PWRI) for backup or a second device.
Contacts are added three ways, trading convenience for trust: an **invite**
(link or QR carrying the self-verifying chain, no server trust), a **contact
card** reached from a descriptor, or an **alias** (convenient, but the resolver
is trusted for the name -> fingerprint mapping).

## Documentation

[`docs/`](docs/README.md) documents this client: what it settles where the
protocol leaves a choice, and the limits of this implementation. The protocol
itself - wire formats, delivery states, admission rules - is specified in
`docs-main` and is common to every client.

## Build

Requires CMake >= 3.20, C++20, OpenSSL >= 3.2 (Argon2id), **SQLCipher 4**, Opus,
libqrencode and Boost. Qt6 (Quick, QuickControls2, Qml, Gui, Multimedia, Widgets,
Network) is what builds the application; without it only the core library and its
tests are built. `common` is a submodule.

SQLCipher 3 will link and will not read an account: the version-4 database format
is not the one it writes, and there is no compatibility pragma from that side. See
[docs/AccountStorage.md](docs/AccountStorage.md).

```bash
git clone --recurse-submodules https://github.com/bazarish/client-qt6
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
```

`packaging/appimage-build.sh` builds the portable Linux AppImage on a Debian 12
base, carrying its own Qt, OpenSSL and SQLCipher.

## Running

```
bazarish-app
```

Accounts are created and opened in the application; it takes no account on the
command line. Two flags exist, both for cases that are not the normal one:

- `--console` - on Windows, take the console it was started from (or open one) so
  the log can be read while it runs.
- `--allow-facade-without-i2p-for-dev-purposes` - talk to a facade over clearnet.
  Named at length because every request then leaves the machine in the clear; it
  exists for a stand on a LAN with no I2P.

`BAZARISH_ACCOUNTS_DIR` overrides the accounts directory, which is how a second
copy runs beside the first.

## Installation layout

```
<root>/
  settings.json     application settings: one JSON document
  notify.wav        replaces the notification sound, if present
  ringtone.wav      replaces the ringtone, if present
  accounts/         one account per file: <id>.db and <id>.key beside it
  i2p/              the embedded router's state
```

`settings.json` holds what belongs to the installation rather than to an account:
the account last in the foreground, the accounts switched off, notifications,
whether the background-activity panel is shown, and the embedded router's
settings (logging, tunnel length, the SAM transport, the clearnet proxy). It is
read at start and rewritten whole on every change, so the file on disk is always
a complete document. A file larger than 64 KiB at that path is refused unread and
the defaults apply: settings run to a few hundred bytes, and nothing at that path
is allowed to decide how much memory the application takes at start.

In **portable mode** the same layout sits in `bazarish_data` beside the
executable instead of in the user's folder.

## Background activity

Long-running async work (adding a contact, sending a message or file, downloading
an attachment, a live call) is surfaced in an **activity panel**, so a slow
operation reads as progress instead of a frozen window. It is **off by default**
and switched on in Global settings: a handle appears on the right edge while
anything is running, and clicking it slides out a panel listing each operation
with a live status, a determinate progress bar for transfers, an elapsed-time
badge and a stop button for a transfer.

For sends the status is the client's own delivery phase - this client carries its
outgoing mail itself (`preparing` the address it leaves from -> `dialing` the
recipient's server -> `sending` -> `retry 2/4`), mapped to human text. The row
lives until the message is handed over or fails, because there is no earlier
handover to report. Adding a contact additionally shows the off-thread card
resolve before the request is sent. See [docs/Sending.md](docs/Sending.md) and
`docs-main/api/Federation.md`.

Implementation: `OperationListModel` (exposed as `App.session.operations` /
`activeOperations`) is driven by `beginOperation`/`updateOperation`/
`finishOperation` in `SessionController`; the QML is `app/qml/OperationsOverlay.qml`
and `app/qml/OperationRow.qml`.
