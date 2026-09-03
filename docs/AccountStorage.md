# Account storage

## Scope

An account is one encrypted database plus the small key record beside it. This
document states the database format this client writes, the library version
required to read it, and what follows from both.

## Files

```
<accounts>/<id>.db     the account: identity, contacts, tokens, transcript
<accounts>/<id>.key    88 bytes: the database key, sealed under the passphrase
```

The database is opened with a random 32-byte key held in the sidecar; the
passphrase never reaches the database itself. The sidecar is sealed with
Argon2id, whose cost parameters it carries, and the reader refuses a file of any
other size or a cost beyond what it will spend.

## SQLCipher

The account database is **SQLCipher 4**. The client neither sets nor overrides
the cipher parameters: what it writes is the library's own version-4 profile,
which is

| Parameter | Value |
|---|---|
| `cipher_page_size` | 4096 |
| `kdf_iter` | 256000 |
| `cipher_kdf_algorithm` | PBKDF2_HMAC_SHA512 |
| `cipher_hmac_algorithm` | HMAC_SHA512 |

A build must therefore link **SQLCipher 4.x**. Version 3 writes a different
format entirely - 1024-byte pages, 64000 iterations, SHA1 - and has no
`cipher_compatibility` pragma to bridge the two. A client linked against
SQLCipher 3 does not read a version-4 account at all: the pages do not decrypt,
the open fails, and the account can only be reported as one that did not open.

Two consequences follow, and both have been met in practice:

- **The packaged build carries its own SQLCipher.** The AppImage is built on the
  oldest supported base, which ships SQLCipher 3, so `packaging/appimage-build.sh`
  builds 4.6.1 from source against the bundled OpenSSL. Without that, an account
  created by a distribution build asks the packaged build for a passphrase it
  never had.
- **An account written by a SQLCipher 3 build stays unreadable.** SQLCipher 4 can
  read such a file with `PRAGMA cipher_compatibility = 3`, and this client does
  not: there is no migration path in either direction, in line with the project's
  rule for a pre-release version.

The compatibility level is not pinned explicitly. It is the library default for
every 4.x release, and pinning it would only matter the day a version 5 changes
that default - at which point the pin, not the discovery, is the smaller change.

## What the file holds, and trimming it

Global settings -> Storage shows what one account keeps on this machine,
conversation by conversation, and drops old history a conversation at a time.

**Where the bytes are.** The transcript's `messages` table holds text and
metadata. Pictures and voice notes are **not** in it: the core keeps them beside
it in the same file under `picture:<id>` and `voice:<id>`. Files that were sent
or received are not in the database at all - they travel directly between the two
clients and land wherever the user saved them.

**What the figures mean.** A conversation's weight is what its rows and its
pictures hold - content, not pages. The file is always larger: it also carries
indexes, per-page overhead and free space, and no query can attribute a page back
to a conversation. The free figure is the freelist counted at the plaintext page
size, so it is a little under what a rewrite actually returns.

**Deletion takes the media with it.** It did not before: clearing a chat removed
its rows and left every picture and voice note it held in the database
permanently. A message is what names its media, so the media goes when the
message does - read from the row before it is removed, in the same statement
sequence. There is no separate collection pass looking for what nothing points
at, and a blob a database already carries under a name no message holds is
therefore not anybody's to remove.

**What a trim does.** It keeps the newest 100 or 1000 messages of a conversation
and removes the rest, with their reactions and their media, in one transaction:
the conversation is trimmed or it is untouched. Newest is by row id, the same key
a conversation pages by, so what is kept is what the view would have shown.
`read_state` and `pinned_chats` are left alone - trimming is not clearing, and
the conversation stays in the list.

A trim is **local to this device**, and that is not a policy but a fact about the
protocol: there is no history backfill between an account's devices. The
device-sync kinds are live echoes of what is happening now, not a request for
what happened before, so a trimmed message does not come back from a sibling
device, and nothing is asked of the person on the other side either.

**The rewrite.** Freed pages stay inside the file until it is rewritten, so a
trim is followed by `VACUUM` and the size on disk falls with it.

That rewrite belongs to the storage window and to nothing else. Its cost is the
size of what is **kept**, not of what went - it reads and writes the whole
database either way - so an ordinary deletion does not perform one: deleting a
single message from a 148 MB account was measured at **4.7 s**, essentially all
of it the rewrite, and the same code path carries a correspondent's "delete for
everyone", which arrives without the user asking for anything. Ordinary deletions
therefore leave their pages in the file to be reused, and the storage window is
where the space is handed back.

`VACUUM` needs free disk space equal to the file, which is checked first, and it
takes the database exclusively, so it can be refused while the account is busy. A
refusal is not a failed trim: the messages are gone, only the space has not come
back, and trimming again returns it. The client says both rather than reporting a
failure. The schema mark is written again afterwards, because an account carrying
any other number is refused at open.

Measured on an account holding 100 000 messages and 2 000 pictures (256 MB):
reading every conversation's weight took 297 ms, trimming every conversation to
the last 100 took 6.9 s, and the rewrite after it took 201 ms and left a 5.4 MB
file.

The rewrite is cheap there because almost nothing was kept: it copies what
survives. On the same account with everything kept (148 MB) it takes 4.7 s, which
is why it is the storage window's operation and not every deletion's.

## The backup bundle

`export` writes one password-sealed file (CMS PWRI) holding what an account *is*,
so a restore is a working device and not a shell of one:

| In the bundle | Why |
|---|---|
| identity and sealing private keys (PEM) | the account itself |
| the I2P **routing master** | the account's address is the account's, not a device's: a restored device that minted its own would take the address away from every contact holding it (`docs-main/Identity.md`) |
| meta and the contact records | who this account is and who it knows |
| the account's avatar and each contact's | the pictures are rows of their own; a bundle carrying only their mime types restored an account with no face at all |
| the block list | a restored account that forgot it would let them all back in |
| **one delivery token per conversation** | see below |

Delivery tokens are one-time write capabilities, so the bundle **moves** one per
conversation rather than copying it: the token leaves the exporting device's
stash as the file is written. Copying them is what made a restored device unable
to write to anybody - it was spending tokens the other device had already spent,
and every send came back `delivery rejected`. One token per contact is exactly
what a restored device needs: it spends it on the errand that buys a batch of its
own (`needsOwnBatch`, `docs-main/DeviceSync.md`), and a second restore of the
same bundle finds the token spent and falls back to borrowing one from the
account's other devices.

A device that ends up without the account's address anyway - an older bundle, or
an account moved another way - does not publish one of its own. On connect it
asks the server which address it serves, and when that is not the one it holds it
asks the account's other devices for the keys (`device.i2p-master-request`). Only
if nobody answers is the user asked, with the two answers that exist: serve this
device's address, or start from a fresh one. Both take the account away from
contacts holding the served address until the user writes to them again, which is
why neither happens silently.

## What a failed open reports

An account that does not open is listed as locked: nothing else about it can be
read, which is what keeping everything in one keyed file is for. Whether it is
locked or merely unreadable is not visible in the interface, so the reason is
written to the log ("account <id> did not open: ..."). A cipher mismatch and a
forgotten passphrase look the same to the user and must not look the same to
whoever reads the log.
