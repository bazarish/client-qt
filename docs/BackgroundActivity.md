# Background activity

## Scope

This document specifies the panel that reports long operations while they run,
and what a row in it says. The panel is off by default.

## The panel

Work that takes time - adding a contact, sending a message or a file,
downloading an attachment, a live call - is reported in an activity panel, so a
slow operation reads as progress rather than as a frozen window. It is switched
on in Global settings. While anything is running a handle appears on the right
edge; it slides out a panel with one row per operation, carrying a live status, a
determinate progress bar for a transfer, how long the operation has been running,
and a stop button for a transfer.

Every operation that reports here is bracketed once: the row opens when the
operation begins, is updated while it runs, and is closed by its outcome. The
panel shows account-level work even when no account is open yet, which is what a
restore from a backup needs.

## What a row says

For a send the status is this client's own delivery phase, because this client
carries its outgoing mail itself: the address it leaves from, dialling the
recipient's server, sending, and the attempt it is on
([Sending.md](Sending.md)). The row lives until the message is handed over or
fails, as there is no earlier handover to report.

Adding a contact also reports the contact-card resolve, which runs off the
interface thread, before the request is sent. The same stages reach the
conversation as a service message ([Receiving.md](Receiving.md)).
