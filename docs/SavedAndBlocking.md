# Saved messages, blocking and per-contact switches

## Scope

Three things this client does with a conversation that the protocol does not
specify: a chat with oneself, a block list, and two switches that narrow the
global settings for one contact. All three are the account's own state, mirrored
to its other devices over the self-addressed device channel specified in
`docs-main/DeviceSync.md`; none of them is ever sent to a correspondent.

## Saved messages

The chat's correspondent is the account's own fingerprint. Nothing else can
occupy that place - a contact is another fingerprint - so the chat cannot be
impersonated, and it is not a contact: it has no card, no tokens, no delivery.

Writing in it costs nothing and dials nothing. The message is stored here and
mirrored to the account's other devices as `device.saved`; a device that was not
running when it was written picks it up with its next sync. A device that joins
the account later starts with an empty chat: what was saved before it existed was
never addressed to it. To have something on a new device, send or forward it
again.

**Text, pictures and voice** are kept. A **file** is refused: a file's bytes
travel directly between the two devices in a conversation, so there would be
nothing for another device to fetch - only a name that opens nothing.

**Clear on all devices** empties it here and everywhere else
(`device.saved-clear`). It is the only action the chat offers, and the chat
itself cannot be deleted or unpinned: it is always the first row in the list.

### The one name a contact may not have

A contact whose name would be `Saved messages` is stored and shown as
`(Contact) Saved messages`. The rule is applied where a name is written, not
where it is drawn - a rename, an alias or invite name, the name a correspondent
chooses for themselves in a contact request, and a rename arriving from another
device - so no path leaves it unmarked. Case and surrounding spaces do not evade
it.

## Blocking

Blocking a correspondent does three things: the tokens they hold are revoked at
this account's server, so their mail stops being accepted rather than being read
and dropped; their messages and contact requests are consumed as they arrive,
before anything is shown or any contact record is touched; and the block is
mirrored to the account's other devices.

It does **not** touch the conversation. The chat and its history stay where they
are, and deleting them is the separate action it already was. Unblocking restores
nothing that was dropped in between, and the correspondent must write again -
they hold no tokens until this account issues more.

**Unblocking hands the tokens back.** Blocking revoked what they held, so a
contact who is unblocked can be written to but cannot answer. The first message
written to them after the block is lifted carries a fresh batch with it - one
delivery, not an errand of its own - and the reverse direction works again from
that moment. A batch that arrives while they still hold tokens is added to what
they have, never in place of it.

**Writing to them unblocks them.** The first thing the user composes into a
blocked conversation - a message, a forward, a picture, a voice note, a file, or a
tap on a command the chat offers - lifts the block first, as if the button had
been pressed, and reaches the account's other devices the same way. Nothing
automatic does this: a read receipt, a token refill, a call signal or a bot's
message is still refused, and the block stands. Writing to somebody is the
plainest way of saying the block was not meant to hold.

The block list outlives the contact it was made on: deleting a blocked contact
leaves the fingerprint blocked. It is shown, with an unblock action, under
**Blocked** in the account settings.

A message from someone who is neither a contact nor asking to become one is
dropped the same way, blocked or not: only a contact, a contact request, or this
account's own devices may put something in front of the user.

## What reaches the account's other devices

Everything that changes what a conversation looks like: a message sent, a picture,
a voice message, an edit, a deletion, a reaction, a chat cleared (either the copy
here or the request to the correspondent), a chat pinned or unpinned, a contact
renamed, removed, blocked or unblocked, its two switches, the saved chat and the
account's own name, its call switch and its read-receipt switch.

What does not: read state and unread counts, which are this device's own -
a message read here is not read there - and this device's errands with a
correspondent, meaning read receipts, call signalling, token refills and the
file-transfer handshake.

Removing a contact revokes their tokens **once**, at the device the removal was
made on. The others apply the removal to their own database and ask the server
for nothing: one revocation is enough, and it is the same server.

## Notifications and calls, per contact

Two switches in a contact's panel, under **Share contact**:

- **Notifications** off: their messages arrive and are counted in the chat list
  as usual, and nothing is announced outside the window - no popup, no sound.
- **Allow calls** off: their call is refused the moment it arrives, exactly as
  the account-wide setting refuses one, and the refusal is written into the chat
  as `Incoming call, refused`. They are told at once rather than left ringing.

Both are exceptions to the global settings and can only take something away: with
notifications off account-wide, a contact's switch does not bring them back.
