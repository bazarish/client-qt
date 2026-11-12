# Outbound delivery

## Scope

The protocol specifies that a message is delivered by the sending client directly
to the destination that serves its recipient, and defines the delivery states and
their sources (`docs-main/Messages.md`, `docs-main/api/Federation.md`). This
document specifies the parameters this client applies to that procedure and the
behaviour it presents to the user.

## Procedure

Every outgoing message - one the user writes, and every control message the
client sends on its own (an acceptance, a presence schedule, a read receipt, an
avatar, a new address, a copy for this account's other devices) - is a letter in
the account's outbox. The outbox is kept in the profile, beside the
conversations, and a letter leaves it only when the recipient's server has
answered. For each letter the client:

1. prepares the outbound destination held for the recipient's destination,
   building one and waiting for its tunnels if none is held;
2. dials the recipient's serving destination and performs one federation deliver
   exchange;
3. accepts the result only if the recipient's server returned a confirmation
   signed over the delivery identifier that was sent;
4. if no answer came, closes the way to that recipient and opens it again on one
   of the causes below; letters to that recipient wait meanwhile, in the order
   they were written.

A letter to this account's own mailbox goes the same way, as a request to this
account's server.

## What opens the way again

| Cause | Effect |
|---|---|
| The recipient's lease set, looked up right after the unanswered attempt or when anything arrives from that contact, was published later than the one held at the failure | the next attempt starts at once |
| 11 minutes since the unanswered attempt (a lease's 10 minutes and 60 seconds of clock difference) | the lease set that attempt used has expired everywhere; the next attempt starts |
| The link came back, or the device moved to another network | every recipient is tried again |
| The contact announced a new address | its letters go to the new address at once |
| The account was opened or switched on | every letter is tried |
| This account's server answered a request (own mailbox only) | its letters go |

The lookup drops what the outbound destination holds of the recipient's lease
set and asks the netDb again. It runs on the router that carries the
destination: the embedded one, or the gateway's over `lease-set-refresh`. An
external router over SAM offers no such lookup, so there the 11 minutes and the
link causes open the way.

## Parameters

| Parameter | Value |
|---|---|
| Dial timeout per attempt | 15 s |
| Reply grace after the envelope is written | 30 s, plus one second per 8 KiB carried |
| Next attempt after no answer, at the latest | 660 s |
| Upper bound on preparing the outbound destination | 180 s |
| Outbound destination, per correspondent: longest life | 3600 s |
| Outbound destination, per correspondent: closed after no delivery for | 600 s |
| Warm destination pool | 2 destinations, 3 tunnels each |
| Call signal worth sending for | 60 s after it was written |
| File request worth sending for | until the request's answer time |
| File offer worth sending for | the 30 minutes the file is served |

An outbound destination is replaced at least hourly, so no long-lived set of
tunnels carries one correspondence, and one that has carried nothing for ten
minutes is closed before then. A
contact request is the exception: its address is dropped as soon as the request
is settled, so every request leaves from an address of its own - the recipient's
server admits one contact request per sending address a day and refuses the next.

## Classification of outcomes

| Outcome | Treatment |
|---|---|
| No reply (no address to send from, no lease set, the stream failed, the envelope was written and nothing came back) | The letter waits; the way to the recipient opens again on a cause above |
| Confirmation absent or not verifiable against the delivery identifier sent | Treated as no reply |
| Final refusal (unknown pass, message too large, contact request too large) | The letter is settled with its reason |
| Transient refusal (storage full, contact rate limit) | The letter is settled with its reason; a resend later may be accepted |
| Signed confirmation | The letter is settled; the message is recorded as stored by the recipient's server |
| A call signal or file letter outlived its time | The letter is settled as out of date |
| The contact was removed | Its letters are settled as withdrawn |

## Reported states

| Protocol state | Presentation in this client |
|---|---|
| preparing | Hollow indicator |
| in flight | Grey indicator; while the way to the recipient is closed the message says it waits for the recipient's server |
| stored | Amber indicator |
| read | Green indicator |
| failed | Red indicator, with the reason under the message and a Resend |
| received | No indicator: the state an incoming message carries |

Red means the recipient's server refused the message, or the message never
reached the outbox. A message that only waits stays grey. A delivery is also
listed in the background-activity panel while it is under way.

Two messages carry no protocol state of their own and are shown at what they
actually are:

- **A note to the saved chat** is green as soon as this account's own server holds
  it, on the device that wrote it and on every other device of the account, which
  sees it arrive through the server that already holds it. It has no
  correspondent to read it and no receipt coming.
- **An echo of what another device sent** appears there only once that device's
  send was stored by the recipient's server, so a message that never arrives
  leaves no copy on the other devices. It arrives in the same amber state the
  sender is showing. Green follows on its own: the read
  receipt is delivered to the account, not to a device, so every device that
  holds the message marks it read when the receipt arrives.

## Restart and resending

A message still grey when the application closes is grey when it opens again:
its letter is in the outbox and goes on its own. One whose letter never reached
the outbox - the application stopped between writing the message and handing it
over - is marked failed, and resending it is the user's decision.

Resending is safe in all cases, including one in which the message was stored by
the recipient's server and only the confirmation was lost: the delivery identifier
is derived from the message and the recipient mailbox, so the recipient's server
recognises the repetition and stores no second copy.

## What admits a message

Every message to a contact presents the **delivery pass** that contact issued to
this account (`docs-main/Contacts.md`). One value, whatever the message weighs: a
line of text and the largest payload the protocol allows are admitted alike, and
what bounds the large one is the 512 KiB message ceiling and the recipient's
storage quota. The pass is not spent, and every device of the account presents
the same one.

A contact this account holds no pass for cannot be written to at all, and that is
an error: a pass does not run down, so an empty one means the dialog was never
established in that direction.

## What a refusal costs

Nothing. A refused delivery leaves this account exactly as able to write as it
was before:

| Refusal | What it says |
|---|---|
| `DELIVERY_REJECTED` | the recipient's mailbox does not hold this pass - it was revoked, so this account has been cut off and the way back is a new contact request |
| `STORAGE_FULL` | the mailbox is full |
| `MESSAGE_TOO_LARGE` | the message is the problem |

`DELIVERY_REJECTED` is not retried: with one pass per correspondent there is
nothing else to offer, so the message is reported failed with a resend control.

## Contact requests in a conversation

A conversation holds **one** invitation plate per direction, however many requests
were actually sent. A repeat carries the name the first attempt gave the request,
so the recipient's server recognises it as that request rather than a new one; a
request that arrives anyway with a name of its own - the correspondent removed
this account and asked again - keeps the plate already there. What it carries
(their routing, the pass they hand over) is applied either way: only the plate is
dropped, because a chat that grows a second one reads as two people asking.

The Agree button on that plate is the contact's state, not the message's: it is
there while the request is unanswered, says what it is doing while the acceptance
waits in the outbox, and goes only when the correspondent's server has confirmed
it holds the batch the acceptance carries. An acceptance their server refuses
brings the button back.

## What rides inside a message

A picture and a voice message travel inside the message: nothing is announced and nothing is fetched, so they arrive with the message even
if the sender goes offline the moment after sending it. Both are made to fit the
protocol's 512 KiB payload.

| Attachment | What the client does with it |
|---|---|
| Picture | Scaled to a long edge of 1600 and re-encoded - JPEG at quality 85, or PNG when the image has an alpha channel. Quality and then the long edge are stepped down, to quality 45 and an edge of 640, until the result is at most 256 KiB |
| Voice message | Opus at the call format - 48 kHz mono, 20 ms frames, each length-prefixed - at most two minutes long, and bounded by the payload limit less an 8 KiB envelope reserve |

A file is transferred directly between the two clients
(`docs-main/api/FileTransfer.md`).

## Outbound addresses

One destination is held per recipient destination for the term stated above, then
discarded; a destination that has carried one correspondent's traffic is not
reassigned to another. Destinations are taken from the warm pool, which is
refilled immediately. Single-use destinations are retained for file transfers and
call media only.
