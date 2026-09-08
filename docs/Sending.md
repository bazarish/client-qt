# Outbound delivery

## Scope

The protocol specifies that a message is delivered by the sending client directly
to the destination that serves its recipient, and defines the delivery states and
their sources (`docs-main/Messages.md`, `docs-main/api/Federation.md`). This
document specifies the parameters this client applies to that procedure and the
behaviour it presents to the user.

## Procedure

For each outgoing message the client:

1. prepares the outbound destination held for the recipient's destination,
   building one and waiting for its tunnels if none is held;
2. dials the recipient's serving destination and performs one federation deliver
   exchange;
3. accepts the result only if the recipient's server returned a confirmation
   signed over the delivery identifier that was sent;
4. repeats step 2 on the schedule below while the outcome is "no answer".

Step 1 is not counted as a delivery attempt and is bounded separately: it is a
local condition, not a property of the recipient.

## Parameters

| Parameter | Value |
|---|---|
| Delivery attempts | 4 |
| Interval between attempts | 2 s, 4 s, 8 s |
| Dial timeout per attempt | 15 s |
| Upper bound on one delivery run | 104 s (the four dials, the three waits, and one reply grace) |
| Grace a reply may add past that bound | 30 s |
| Upper bound on preparing the outbound destination | 180 s |
| Outbound destination term, per correspondent | 600 s |
| Warm destination pool | 2 destinations, 3 tunnels each |

The bound on a run is the sum of what the run may spend, not a number chosen
beside it: an attempt is made only when the budget can pay for its wait and for
the whole of its dial, so a bound shorter than the schedule would quietly cost
the last attempts. It is what terminates an attempt in which the peer accepts the
stream and returns no reply - and because that one wait can consume most of the
budget, a run against such a peer legitimately makes fewer than four attempts.

What the user is told then says which of the two happened and how far the run
got: an attempt that the budget cannot pay for is not announced, and the failure
carries the number of attempts actually made ("2 of 4 tries"). A destination that
took the envelope and said nothing is reported as that, not as one that could not
be reached - the message may well be stored, with only the confirmation missing.

## Classification of outcomes

| Outcome | Treatment |
|---|---|
| No reply (dial failed, stream failed, reply unreadable) | Retried until the attempts or the run bound are exhausted; the failure names how many attempts were made |
| The envelope was written and no reply came | Retried the same way, and reported as a far side that did not answer rather than one that could not be reached |
| Confirmation absent or not verifiable against the delivery identifier sent | Treated as no reply |
| Typed refusal (unknown pass, message too large, contact request too large, storage full, contact rate limit) | Final on the first attempt; reported with its reason |
| Signed confirmation | Delivery complete; the message is recorded as stored by the recipient's server |

## Reported states

| Protocol state | Presentation in this client |
|---|---|
| preparing | Hollow indicator |
| in flight | Grey indicator; during retries the message reports the attempt in progress |
| stored | Amber indicator |
| read | Green indicator |

A delivery run is also listed in the background-activity panel for its duration.

Two messages carry no protocol state of their own and are shown at what they
actually are:

- **A note to the saved chat** is green as soon as this account's own server holds
  it. There is no correspondent to read it and no receipt coming, so amber would
  be a wait for something that never arrives - on the device that wrote it and on
  every other device of the account, which sees it arrive through the server that
  already holds it.
- **An echo of what another device sent** appears there only once that device's
  send was stored by the recipient's server. Echoing at the moment of writing put
  a copy on every other device for a message that never arrived - and one per
  retry - so the echo now waits for the delivery it echoes, and arrives in the
  same amber state the sender is showing. Green follows on its own: the read
  receipt is delivered to the account, not to a device, so every device that
  holds the message marks it read when the receipt arrives.

## Failure and resending

When the attempts are exhausted the message is marked failed, with the reason,
and a resend control is offered. The client performs no automatic resend.

No outbound queue is written to disk. This is deliberate: such a queue would
constitute a record, held by the client, of the correspondents its user writes to.
Consequently, a message whose delivery was in progress when the application closed
is reported as failed at the next start, and resending it is a user decision.

Resending is safe in all cases, including one in which the message was stored by
the recipient's server and only the confirmation was lost: the delivery identifier
is derived from the message and the recipient mailbox, so the recipient's server
recognises the repetition and stores no second copy.

## What admits a message

Every message to a contact presents the **delivery pass** that contact issued to
this account (`docs-main/Contacts.md`). One value, whatever the message weighs: a
line of text and the largest payload the protocol allows are admitted alike, and
what bounds the large one is the 512 KiB message ceiling and the recipient's
storage quota. The pass is not spent, so nothing has to be counted, drawn or
saved up, and every device of the account presents the same one.

A contact this account holds no pass for cannot be written to at all, and that is
an error rather than a wait: nothing runs a pass down, so an empty one means the
dialog was never established in that direction.

## What a refusal costs

Nothing. A pass is not a payment, so a refused delivery leaves this account
exactly as able to write as it was before:

| Refusal | What it says |
|---|---|
| `DELIVERY_REJECTED` | the recipient's mailbox does not hold this pass - it was revoked, so this account has been cut off and the way back is a new contact request |
| `STORAGE_FULL` | the mailbox is full |
| `RECIPIENT_SERVER_UNREACHABLE` | nothing was presented at all |
| `MESSAGE_TOO_LARGE` | the message is the problem |

`DELIVERY_REJECTED` used to be worth retrying, because a stash held many
capabilities and one of them being stale said nothing about the others. With one
pass per correspondent there is nothing else to offer, so the message is reported
failed with a resend control and no ladder is walked.

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
is in the air, and goes only when the correspondent's server has confirmed it
holds the batch the acceptance carries. An acceptance that never lands brings the
button back rather than leaving a contact that looks answered on this side alone.

## Outbound addresses

One destination is held per recipient destination for the term stated above, then
discarded; a destination that has carried one correspondent's traffic is not
reassigned to another. Destinations are taken from the warm pool, which is
refilled immediately. Single-use destinations are retained for file transfers and
call media only.
