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

## What a failed open reports

An account that does not open is listed as locked: nothing else about it can be
read, which is what keeping everything in one keyed file is for. Whether it is
locked or merely unreadable is not visible in the interface, so the reason is
written to the log ("account <id> did not open: ..."). A cipher mismatch and a
forgotten passphrase look the same to the user and must not look the same to
whoever reads the log.
