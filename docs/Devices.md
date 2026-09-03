# Devices and the account they share

## Scope

What this client does with the devices of one account: how a second one starts,
what it asks the first for, and what leaving looks like. The protocol's side of
this - the self-channel, the message types, the rule that an account has one
address and that delivery tokens are never shared - is in `docs-main`
(`DeviceSync.md`, `Identity.md`).

## What has been read

Reading is the account's act, not the device's, so a message read on one device is
not offered as new on the next one: the reader tells its siblings how far it has
read, over the self-channel, and they fold the mark into their own (`device.read`
in `docs-main/DeviceSync.md`).

What this client settles, where the protocol leaves it open:

- **When it tells them.** Not per message - reading a conversation advances the
  mark once per bubble that scrolls past, and each notice is an item in the
  account's own mailbox. It waits **4 seconds** after the reading settles, and
  sends at once when the conversation is left, so what a reader saw is owed to
  the other devices before they are asked about anything else.
- **What is lost if it does not.** Closing the application inside that wait drops
  the mark; the messages stay unread on the other device, which is the harmless
  direction.

## Starting a second device

A device starts from a backup of the account (`AccountStorage.md`). Restoring is
slow enough to be worth watching - the bundle is unsealed, a keyed database is
written, and the account is laid out - so it runs off the interface thread and is
listed in the background-activity panel while it does, like every other slow
thing. The panel shows account-level work even when no account is open yet - when
it is switched on, which it is not by default (`../README.md`).

What the restored device then does, in order:

1. **Takes the account's address from the backup.** It publishes nothing of its
   own: the address in the bundle is the one every contact holds.
2. **Asks the server which address it serves.** Matching what the backup carried
   means there is nothing to reconcile. An empty answer - the server holds no
   address for this account - means this one is published as it stands.
3. **Asks the account's other devices** for the keys, when the served address is
   neither. Any device holding the master answers.
4. **Asks the user**, and only then. The window says which address the server
   serves and that this device has no keys for it, and offers the two answers
   there are: serve this device's address, or start from a fresh one. Both leave
   contacts holding the served address unable to reach the account until the user
   writes to them again, so neither is done quietly.

5. **Asks for the address book** (once per device), because a contact is reached
   by destination and read by capability and there is no lookup that turns a
   fingerprint into either.

6. **Keeps the delivery token the backup handed it.** One per conversation,
   moved rather than copied, and it is not spent on an errand: the first message
   the user writes carries the same request (a low-stash flag and a prepaid reply
   token, addressed to this device), so it goes out and brings a batch back.
   With none at all - a second restore of the same bundle, or a conversation that
   had none - it asks the account's other devices for one, once per run.

## When the address changes

Publishing an address - at connect, or from either answer to the question above -
also writes it to the account's own mailbox (`device.i2p-master`), so the other
devices keep the account on one address. A device that holds no master takes it.
A device that holds a **different** one takes it too, but only after its own
server confirms that address is the one being served: the account has moved, and
a device still announcing the old address in its outgoing routing would keep its
contacts writing where nobody listens. The stale delegation goes with the stale
master, because it was signed by it.

That check is what makes the adoption safe. A copy of an older master sitting in
the mailbox, or replayed into it, does not match what the server serves and is
ignored with a line in the log.

## When the two disagree anyway

The account window shows the address this device holds the keys for. Beside it,
when the server answers with a different one, it shows **that** address and says
so: another device published it, contacts write to whichever they know, and until
the two agree some mail reaches nobody. The status poll asks the server every ten
seconds while the window is open, so the warning appears without reopening
anything, and one button under it publishes this device's address instead.

This is the only place the difference is visible: the address a device shows is
what it believes it published, and belief is not what mail is delivered to.

## Leaving

The device list in the account window carries one line per device, and a quiet
`Forget` beside every device that is not this one. It asks first: the server
stops keeping mail for that device and drops the queue it holds, and anything in
that queue no other device has collected is gone. It is not a ban - the next time
that device connects it registers again and works as before.

Deleting the account offers the same distinction. **Delete everywhere** ends the
account on its server: the address, the mailbox, everything in it. **This device
only** asks the server for nothing but forgetting this device, and leaves the
account, its address and its mail standing for the user's other devices. The
second was previously reachable only for a profile that could not be unlocked; it
is now a choice on its own.
