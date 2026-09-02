# Connection log

Status: implemented.

Account window -> "Server connection" -> **Connection log**. It shows what this
account did on the wire and what came back: the account's own server calls, the
mail it sent to correspondents, and the far side's answer to each.

It exists because nothing else answers "did that actually leave, and did anyone
agree to it?" - a bubble says "sent", and the log on stderr is not there in a
packaged build.

## What is in it

| Line | Where it comes from | What its status means |
|---|---|---|
| `POST /v1/messaging/self`, `GET /v1/messaging/pending`, … | every call to this account's server | the HTTP status the server answered with, or the transport failure |
| `text to Bob (a1b2c3)` | mail handed to the courier | `sending`, and a second line when the run ends: `stored` once the recipient's server signed for the envelope, or `failed: <code>`. Two lines because the attempt schedule outlives the send by up to a minute |
| `POST /v1/messaging/self (device.account-name)` | a change mirrored to this account's other devices | the server's status, like any other call - the kind rides on the call itself, because from the outside every device sync is the same POST |
| `receipt from Bob (a1b2c3)` | an item this account fetched | (none) |
| `unreadable item` | a pending entry that could not be opened | `dropped` - it is acked so the mailbox unblocks |
| `unsigned item`, `item signed by another key` | an item whose author could not be established | `dropped` - admission is not authorship, so it never reaches a chat |
| `device message from a contact` | a device-sync type sent by somebody who is not one of this account's devices | `dropped`, with the type it claimed - these change settings and write into the address book, so only this account's own devices may send them |

Mail to a correspondent never goes through this account's server: it is dialled
over I2P from a destination this client holds, so the confirmation on those lines
is the **recipient's** server signing for the delivery, not ours.

## What is not in it

- **No message text**, in any line, including the detail column.
- **No full fingerprints or destinations** - a correspondent is named by the
  local name for them plus the first characters of their fingerprint. The window
  is meant to be screenshotted into a bug report.
- **No empty polls.** The client holds a long poll on the server; a round trip
  that brought nothing back is not recorded, or it would be the only thing the
  log ever showed.

## Its limits

The log holds the last **100 events per account**, in memory only: switching
accounts does not mix them, closing the application loses them, and nothing is
written to disk. "Copy all" puts the visible lines on the clipboard; "Clear"
empties the ring.
