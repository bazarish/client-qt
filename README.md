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

Messages are end-to-end encrypted to the peer's user sealing key. The **first**
contact request is encrypted too, using the peer's sealing **prekey** fetched
from its serving server (`GET /v1/account/contact`) - there is no plaintext
first message. The server stores only opaque ciphertext.

The private keys are stored as PEM under the state directory and may be
**encrypted at rest** with a passphrase (AES-256-CBC). The whole state can be
**exported** into a single password-encrypted bundle (CMS PWRI) for backup or
device migration. Contacts can be added three ways, trading convenience for
trust (see the spec's *Out-of-band invites*): a fully offline **invite**
(link or QR carrying the self-verifying trust chain, no server trust), a raw
**fingerprint**, or a **username** (convenient, but the resolver is trusted for
the name->fingerprint mapping).

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
bazarish-client init       <state> <host> <port> <server-fp> [base-path]
bazarish-client subscribe  <state> [days]
bazarish-client whoami     <state>
bazarish-client alias      <state> <name>
bazarish-client invite     <state>
bazarish-client request    <state> <peer-fp> <text> [peer-host peer-port [base-path]]
bazarish-client add-invite <state> <invite-file> <text>
bazarish-client add-user   <state> <alias> <text> [host port [base-path]]
bazarish-client send       <state> <peer-fp> <text>
bazarish-client sync       <state>
bazarish-client export     <state> <out-file>
bazarish-client import     <in-file> <state>
```

`<state>` is a directory holding this client's identity and contacts.
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
bazarish-gui <state>                              # open an existing client
bazarish-gui <state> <host> <port> <server-fp>    # create and subscribe a new one
```
