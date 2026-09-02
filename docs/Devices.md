# Devices and the account they share

## Scope

What this client does with the devices of one account: how a second one starts,
what it asks the first for, and what leaving looks like. The protocol's side of
this - the self-channel, the message types, the rule that an account has one
address and that delivery tokens are never shared - is in `docs-main`
(`DeviceSync.md`, `Identity.md`).

## Starting a second device

A device starts from a backup of the account (`AccountStorage.md`). Restoring is
slow enough to be worth watching - the bundle is unsealed, a keyed database is
written, and the account is laid out - so it runs off the interface thread and is
listed in the background-activity panel while it does, like every other slow
thing. The panel shows account-level work even when no account is open yet.

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

6. **Buys its own delivery tokens.** The backup hands it one token per
   conversation - moved, not copied - and it spends each on the errand that
   returns a batch addressed to this device.

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
