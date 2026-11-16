# Account storage

## Scope

An account is one encrypted database plus the small key record beside it. This
document states the database format this client writes, the library version
required to read it, and what follows from both.

## Files

```
<accounts>/<id>.db     the account: identity, contacts, passes, transcript
<accounts>/<id>.key    88 bytes: the database key, sealed under the passphrase
```

The database is opened with a random 32-byte key held in the sidecar; the
passphrase never reaches the database itself. The sidecar is sealed with
Argon2id, whose cost parameters it carries, and the reader refuses a file of any
other size or a cost beyond what it will spend.

An account without a passphrase is encrypted the same way, with a random 32-byte
key that cannot be guessed. Its sidecar seals that key under a fixed secret every
client knows, at a low Argon2id cost: 8 MiB and one pass, against 64 MiB and
three passes under a passphrase. The database file alone does not open; the
database together with its `.key` file opens without any passphrase. That fixed
secret cannot be chosen as a passphrase: the window refuses it, and so does the
core.

**Changing the passphrase** closes the account and re-encrypts the whole
database under a new random key: `sqlcipher_export` streams it into
`<id>.db-rekey`, the new key is sealed under the new passphrase into
`<id>.key-rekey`, and the two are renamed over `<id>.db` and `<id>.key`, the
database first. Renaming the database is the commit. The next open finishes an
interrupted change: a `<id>.db-rekey` still there means the change did not
commit, and the new files are dropped; a lone `<id>.key-rekey` means it did, and
the key is put in place. The account then opens again. An old copy of the `.key`
file, with the old passphrase, does not open the new database.

**What is deleted leaves the file.** Every connection to the database runs with
`secure_delete`, so a deleted row's bytes are overwritten, and the write-ahead
log is emptied after each deletion and edit and after epoch keys are erased, so
the old content does not stay there either. A deletion that cannot empty the log
because another connection is reading it reports a failure.

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

A build must therefore link **SQLCipher 4.x**. A client linked against
SQLCipher 3 does not read a version-4 account: the pages do not decrypt, the open
fails, and the account is reported as one that did not open.

Two consequences follow:

- **The packaged build carries its own SQLCipher.** The AppImage is built on the
  oldest supported base, which ships SQLCipher 3, so `packaging/appimage-build.sh`
  builds 4.6.1 from source against the bundled OpenSSL.
- **An account written by a SQLCipher 3 build is not read.** The client opens
  the version-4 profile only and does not set `cipher_compatibility`.

The compatibility level is the library default of every 4.x release.

## What the file holds, and trimming it

Global settings -> Storage shows what one account keeps on this machine,
conversation by conversation, and drops old history a conversation at a time.

**Where the bytes are.** The transcript's `messages` table holds text and
metadata. Pictures and voice notes are kept beside it in the same file under
`picture:<id>` and `voice:<id>`. Files sent or received stay outside the
database: they travel directly between the two clients and land wherever the
user saved them.

**What the figures mean.** A conversation's weight is what its rows and its
pictures hold - content, not pages. The file is always larger: it also carries
indexes, per-page overhead and free space, and no query can attribute a page back
to a conversation. The free figure is the freelist counted at the plaintext page
size, so it is a little under what compacting actually returns.

**Deletion takes the media with it.** A message is what names its media, so the
media goes when the message does - read from the row before it is removed, in the
same statement sequence.

**What a trim does.** It keeps the newest 100 or 1000 messages of a conversation
and removes the rest, with their reactions and their media, in one transaction:
the conversation is trimmed or it is untouched. Newest is by row id, the same key
a conversation pages by, so what is kept is what the view would have shown.
`read_state` and `pinned_chats` are left alone, and the conversation stays in
the list.

A trim is **local to this device**. The device-sync kinds are live echoes of what
is happening now, so a trimmed message does not come back from a sibling device,
and nothing is asked of the person on the other side.

**Compacting.** Deleted rows leave free pages inside the file, so a trim is
followed by handing them back to the disk. The same operation is a button of its
own in the storage window - "Compact the database" - because it removes nothing:
it only hands back what earlier deletions already freed, which makes it the one
non-destructive action there.

Every profile database is created in SQLite's incremental vacuum mode
(`auto_vacuum = INCREMENTAL`), the copy a passphrase change re-encrypts into and
an imported account included; SQLite takes the mode only while a file has no
page yet. Compacting runs `PRAGMA incremental_vacuum` in steps of 256 pages, 1 MiB
at 4 KiB pages, each step one short write lock. After each step it waits as long
as the step took, so a writer the step held off gets its turn, and it goes on
until no free page is left; then it checkpoints the write-ahead log. Incremental
vacuum gives free pages back to the disk; it does not defragment the pages that
stay. A database made without incremental mode is refused with that reason, and
so is a step that leaves as many free pages as before.

Handing pages back belongs to the storage window and to nothing else. An ordinary
deletion, which also carries a correspondent's "delete for everyone" arriving
without the user asking for anything, leaves its pages in the file to be reused,
and the storage window is where the space is handed back.

A trim and a compaction run on a connection of their own, off the core's thread,
so the account goes on receiving and sending meanwhile. There is no check of free
disk space first and no exclusive lock. The storage window says what is running
and holds until it ends, with no percentage. A failed compaction after a trim
leaves the trim done: the messages are gone, only the space has not come back,
and trimming again returns it. The client says both.

Measured on an account holding 100 000 messages and 2 000 pictures (256 MB):
reading every conversation's weight took 297 ms, and trimming every conversation
to the last 100 took 6.9 s.

## The backup bundle

`export` writes one password-sealed file (Argon2id and AES-256-GCM, `docs-main/Identity.md`, Algorithms) holding what an account *is*,
so a restore is a working device and not a shell of one:

On Android the app opts out of the system's cloud backup, which would otherwise
copy the account folder, keys included, to Google.

| In the bundle | Why |
|---|---|
| identity and sealing private keys (PEM) | the account itself |
| the I2P **routing master** | the account's address is the account's, not a device's: a restored device that minted its own would take the address away from every contact holding it (`docs-main/Identity.md`) |
| meta and the contact records | who this account is and who it knows |
| the account's avatar and each contact's | the pictures are rows of their own, so the bundle carries them in full |
| the block list | a restored account that forgot it would let them all back in |
| **each conversation's delivery pass** | see below |

A delivery pass is not spent, so the bundle **copies** it: the exporting device
goes on writing and the restored one can write from its first sync.

A device that ends up without the account's address anyway - an account moved
another way - does not publish one of its own. On connect it
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
