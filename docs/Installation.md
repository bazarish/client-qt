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

## bazarish:// links

The client registers itself as the handler for the scheme and is given the link
the way each desktop gives one: as an argument on Linux and Windows, as an open
event on macOS, which starts no second copy. A copy that finds the account folder
already claimed hands the link to the one holding it and exits.

What the link carries decides where it opens:

| Link | Where it goes |
|---|---|
| `bazarish://invite?…` | the add-contact sheet, with the invite in it |
| `bazarish://server?…` | the connect form, with the server in it |
| `bazarish://pair?…` | the wizard that enrols this device, with the link in it |

The first two act on an open account, so a link that arrives before one is open
waits for it rather than going nowhere. A link of any other shape is refused on
screen and written to the log.

The association is registered where each desktop keeps it: the desktop entry
carries `MimeType=x-scheme-handler/bazarish` on Linux, the bundle's property list
carries `CFBundleURLSchemes` on macOS, and the registry carries the scheme under
`HKEY_CURRENT_USER\Software\Classes` on Windows.
