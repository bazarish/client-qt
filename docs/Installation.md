# Installation layout

## Scope

This document specifies what the installation keeps outside any one account:
where its root is, what the settings document holds, and what the command line
takes. The account files themselves are specified in
[AccountStorage.md](AccountStorage.md).

## The root

| Platform | Root |
|---|---|
| Linux | `$HOME/.local/share/bazarish` |
| Windows | `%APPDATA%\Bazarish` |
| Portable mode | `bazarish_data` beside the executable |

```
<root>/
  settings.json     application settings: one JSON document
  notify.wav        replaces the notification sound, if present
  reaction.wav      replaces the reaction sound, if present
  ringtone.wav      replaces the ringtone, if present
  accounts/         one account per file: <id>.db and <id>.key beside it
  i2p/              the embedded router's state
```

Portable mode is a marker file, `.bazarish.portable`, beside the executable. The
switch in Global settings moves the whole layout, closes every account and needs
the application started again. The three sound files are specified in
[Sounds.md](Sounds.md).

## settings.json

The document holds what belongs to the installation rather than to an account:
the account last in the foreground, the accounts switched off, notifications,
whether the background-activity panel is shown, the interface language, and how
I2P is carried - which of the three transports, tunnel length, engine logging,
the clearnet proxy, and a private gateway's address and key
([I2pRouter.md](I2pRouter.md)).

It is read at start and rewritten whole on every change, so the file on disk is
always a complete document. A file larger than **64 KiB** at that path is refused
unread and the defaults apply: settings run to a few hundred bytes, and nothing
at that path is allowed to decide how much memory the application takes at start.

## The command line

Accounts are created and opened in the application; it takes no account on the
command line. Two flags exist, both for cases that are not the normal one:

- `--console` - on Windows, take the console the application was started from, or
  open one, so the log can be read while it runs.
- `--allow-facade-without-i2p-for-dev-purposes` - talk to a facade over clearnet.
  The name is long because every request then leaves the machine in the clear;
  the flag exists for a stand on a LAN with no I2P.

`BAZARISH_ACCOUNTS_DIR` overrides the accounts directory, which is how a second
copy runs beside the first.
