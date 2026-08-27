# Sending, as this client does it

The protocol says an outgoing message is delivered by its sender's client
straight to the recipient's server, and what the four delivery states mean
(`docs-main/Messages.md`, `docs-main/api/Federation.md`). What follows is this
client's side of that: the numbers it chose and what the user sees.

## Attempts

One send gets **four attempts inside about a minute** - a dial and one frame
exchange each, with waits of 2, 4 and 8 seconds between them, and a hard limit of
75 seconds on the whole run so a peer that accepts a stream and then says nothing
cannot stretch it. Preparing the local address to send from is not one of the
four and is not inside that budget: building tunnels is this device's condition,
not the recipient's.

Only a failure to *get* an answer is repeated. An answer that refuses (an invalid
token, a message over the size cap, a full mailbox, too many contact requests) is
final on the first attempt and shown with its reason.

## What the ticks mean here

| Tick | This client is |
|---|---|
| hollow grey ring | preparing the address this message leaves from |
| grey | dialling the recipient's server and handing the envelope over |
| yellow | done: the recipient's server signed for it |
| green | told by the recipient's client that it was read |

While the attempts run, the bubble says which one it is on ("trying again (3 of
4)"); the background-activity panel shows the same run as one row until it ends.

## Failure, and resending

When the last attempt is spent the message turns **red** with the reason and a
**Resend** the user presses. Nothing resends by itself.

There is no outbound queue on disk - deliberately, since a queue would be this
client's own record of who its user writes to. So a client closed mid-send comes
back with that message red: on the next start an unfinished send is one that did
not happen, and sending it again is the user's decision.

A resend costs nothing even when the first copy did arrive and only its
confirmation was lost: the delivery id is derived from the message and the
mailbox, so the recipient's server recognises the repeat, stores no second copy
and spends no second token. The token, though, is spent when the envelope leaves
rather than when it lands - one that may already have been consumed at the far
end must never be offered to a different message.

## Addresses it sends from

One destination per correspondent, held for ten minutes and then dropped, taken
from a pool of two kept warm. One-time destinations are left to file transfers and
calls. A destination that has carried one correspondent's mail is never handed to
another.
