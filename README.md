# Bazarish - client-qt6

The client for the [Bazarish](https://github.com/bazarish/docs-main) messenger.
One library with three layers on top:

- **Core library** - `ApiClient` (hybrid-signed HTTP transport over the client
  API), `Client` (typed endpoint wrappers + sealed-envelope construction) and
  `Session` (stateful: identity, sealing key, contacts, one-time tokens, E2E
  encryption, mailbox sync).
- **CLI** - `bazarish-client`, a stateful command-line client.
- **GUI** - `bazarish-gui`, a Qt6 desktop app (built only when Qt6 Widgets is
  present), driving the same `Session`.

A picture and a voice message ride **inside** the message, so they arrive with
it: nothing is announced, nothing is fetched, and the mailbox holds them like any
other message even if the sender goes offline the moment after sending. A picture
is scaled and re-encoded first (long edge 1600, at most 256 KiB); a voice message
is Opus at the call format - 48 kHz mono, 20 ms frames, each length-prefixed -
which is a few hundred KiB for a minute or two. Both sit inside the protocol's
512 KiB message limit.

The sealed body of a message is **CBOR**, not JSON text. It is the same document
with the same field names, but binary values travel as themselves - which is what
lets a small attachment ride inside the message it belongs to instead of being
announced and fetched. Base64 would have cost a third of every such payload (1.33x
measured), while compressing it buys nothing: pictures and Opus audio are already
entropy-coded. Large files keep the announcement-and-fetch path, where size is
bounded by nothing.

Messages are end-to-end encrypted to the peer's user sealing key. The **first**
contact request is encrypted too, using the peer's sealing **prekey** fetched
from its serving server (`GET /v1/account/contact`) - there is no plaintext
first message. The server stores only opaque ciphertext.

The private keys are stored as PEM under the account directory and may be
**encrypted at rest** with a passphrase (AES-256-CBC). The whole state can be
**exported** into a single password-encrypted bundle (CMS PWRI) for backup or
device migration. Contacts can be added three ways, trading convenience for
trust (see the spec's *Out-of-band invites*): a fully offline **invite**
(link or QR carrying the self-verifying trust chain, no server trust), a raw
**fingerprint**, or a **username** (convenient, but the resolver is trusted for
the name->fingerprint mapping).

## Documentation

`docs/` documents this client: the embedded router and its settings
([docs/I2pRouter.md](docs/I2pRouter.md)) and outbound delivery
([docs/Sending.md](docs/Sending.md)). The protocol itself is specified in
`docs-main` and is common to all clients.

## Build

Requires CMake >= 3.20, C++20, OpenSSL >= 3.0; Qt6 Widgets is optional (enables
the GUI). `common` is a submodule.

```bash
git clone --recurse-submodules https://github.com/bazarish/client-qt6
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
```

## CLI

```
bazarish-client init       <account> <host> <port> <server-fp> [base-path]
bazarish-client subscribe  <account> [days]
bazarish-client whoami     <account>
bazarish-client alias      <account> <name>
bazarish-client invite     <account>
bazarish-client request    <account> <peer-fp> <text> [peer-host peer-port [base-path]]
bazarish-client add-invite <account> <invite-file> <text>
bazarish-client add-user   <account> <alias> <text> [host port [base-path]]
bazarish-client send       <account> <peer-fp> <text>
bazarish-client sync       <account>
bazarish-client export     <account> <out-file>
bazarish-client import     <in-file> <account>
```

`<account>` is a directory holding this client's identity and contacts.
`<host> <port>` point at a facade. For a cross-server first contact, give the
peer's facade host/port to `request` so its prekey and server card can be
looked up; omit them when the peer is on the same facade. `invite` prints a
`bazarish://` link plus a multi-frame QR sequence carrying the full trust
chain; `add-invite` consumes such a link (read from a file, since it is large);
`add-user` resolves a username via the service node. `send`/`sync` drive the
conversation.

Environment variables:

- `BAZARISH_PASSPHRASE` - when set, `init` encrypts the key PEMs at rest and
  every other command needs it to open the state.
- `BAZARISH_EXPORT_PASSWORD` - required by `export`/`import`; protects the
  bundle independently of the at-rest passphrase.

## GUI

```
bazarish-gui <account>                              # open an existing client
bazarish-gui <account> <host> <port> <server-fp>    # create and subscribe a new one
```

## Background activity

Long-running async work (adding a contact, sending a message or file, downloading
an attachment, a live call) is surfaced in a unified **activity panel** so a slow
operation reads as progress instead of a frozen window. A small translucent handle
appears on the right edge while anything is running (opaque on hover); clicking it
slides out a panel listing each operation with a live, human-readable status, a
determinate progress bar for transfers, and an elapsed-time badge.

For sends the status is the client's own delivery phase - this client carries its
outgoing mail itself (`preparing` the address it leaves from -> `dialing` the
recipient's server -> `sending` -> `retry 2/4`), mapped to human text. The row
lives until the message is handed over or fails, because there is no earlier
handover to report. Contact-add additionally shows the off-thread card resolve
(`Resolving recipient over i2p...` with a live timer) before the request is sent.
See `docs-main/api/Federation.md` for the delivery flow.

Implementation: `OperationListModel` (exposed as `App.session.operations` /
`activeOperations`) is driven by `beginOperation`/`updateOperation`/`finishOperation`
in `SessionController`; the QML lives in `app/qml/OperationsOverlay.qml` and
`app/qml/OperationRow.qml`.
