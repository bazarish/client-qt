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

On the desktop both halves run in one process. On Android they run in two
([On Android](#on-android)). The core is built as a library of
its own, without Qt Quick and Qt QML, so nothing in it can come to depend on the
window.

## Frames

The halves speak in frames. A frame names a scope - an account, `app` or `i2p` -
a signal, and the signal's arguments in `QDataStream` form, version Qt 6.8. A
frame that does not read to its end, or names a signal the other side does not
have, is refused whole. Within one process that is a fault, and the application
ends.

The same frames cross a local socket when the core runs alone, as it does on
Android and in the two-process test. There a frame is preceded by its length and
is at most **16 MiB**, and a window that falls **64 MiB** behind is let go.

## A window attaching

A window may attach to a core that has been running for a while. It is told first
which accounts are open, then the last value of every event that states something
rather than reports it - identity, presence, contacts, the server, the call,
avatars, settings and the router's state - in the order they happened, then the
operations still running, and last that it has been told everything. Until then
it shows an empty screen. Every start of the application attaches its window this
way.

## On Android

The core is a foreground service in a process of its own, `:core`, started by the
window or at boot. The window is the activity's process and ends with it, so
swiping the app from the recent apps leaves the core and its tunnels standing,
and the next window attaches to them in seconds.

A window starts the core and connects once the core answers that it listens; the
socket is the name `bazarish-core-` followed by a digest of the installation
root. A window that loses its core starts it again and attaches anew, unless the
core said it was quitting: Quit in the shade ends both processes.

An incoming call opens the window over the lock screen at once, before the window
has heard of the call; the call screen follows as soon as the window has been
told.

Only the app itself reaches the core. The socket file lies in the app's private
cache directory, which no other app can enter, and whoever connects there gets
the whole account. The service is not exported. The notices open the window
through `NoticeEntry`, an entry other apps cannot start. The launcher entry,
which any app can start, opens the window and does nothing else: no answering,
no showing over the lock screen, no opening a chat.
