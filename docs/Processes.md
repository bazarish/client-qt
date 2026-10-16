# The core and the window

## Scope

This document records how the client divides into a core that keeps the accounts
and a window that shows them, and what passes between the two.

## The halves

The core holds what has to go on with no window: the accounts, the I2P router,
receiving and storing mail, sending, calls and their sound, notifications, and
every write to an account's database and to `settings.json`. The window holds the
interface: its models, paging, search, drafts and voice recording. It reads a
conversation from the account's database itself, read-only, with the key the core
hands it, and asks the core for everything else.

On the desktop both halves run in one process. The core is built as a library of
its own, without Qt Quick and Qt QML, so nothing in it can come to depend on the
window.

## Frames

The halves speak in frames. A frame names a scope - an account, `app` or `i2p` -
a signal, and the signal's arguments in `QDataStream` form, version Qt 6.8. A
frame that does not read to its end, or names a signal the other side does not
have, is refused whole. Within one process that is a fault, and the application
ends.

The same frames cross a local socket when the core runs alone, as it does in the
two-process test. There a frame is preceded by its length and is at most
**16 MiB**, and a window that falls **64 MiB** behind is let go.

## A window attaching

A window may attach to a core that has been running for a while. It is told first
which accounts are open, then the last value of every event that states something
rather than reports it - identity, presence, contacts, the server, the call,
avatars, settings and the router's state - in the order they happened, then the
operations still running, and last that it has been told everything. Until then
it shows an empty screen. Every start of the application attaches its window this
way.
