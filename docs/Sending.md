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
| Upper bound on one delivery run | 75 s |
| Upper bound on preparing the outbound destination | 180 s |
| Outbound destination term, per correspondent | 600 s |
| Warm destination pool | 2 destinations, 3 tunnels each |

The upper bound on a run also terminates an attempt in which the peer accepts the
stream and returns no reply.

## Classification of outcomes

| Outcome | Treatment |
|---|---|
| No reply (dial failed, stream failed, reply unreadable) | Retried until the attempts or the run bound are exhausted |
| Confirmation absent or not verifiable against the delivery identifier sent | Treated as no reply |
| Typed refusal (invalid token, message too large, contact request too large, storage full, contact rate limit) | Final on the first attempt; reported with its reason |
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
recognises the repetition, stores no second copy and consumes no second token.

## Delivery-token accounting

The one-time delivery token is deducted from the local stash when the envelope is
handed to the courier, not when delivery is confirmed. After the attempts are
exhausted the client cannot establish whether the recipient's server stored the
message; a token that may already have been consumed there must not be offered to
a subsequent message, which would then be rejected. A resend of the same message
consumes no further token, because the recipient's server matches it by delivery
identifier before the token is examined.

## What a refusal costs the stash

A refusal by the recipient's server is an answer about the capability, not about
the network, so only one of them touches the stash:

| Refusal | What it says | What the client does |
|---|---|---|
| `DELIVERY_REJECTED` | the server would not take **that token** | sends the message again with the next token in the stash |
| `STORAGE_FULL` | the mailbox is full; the token is untouched, and the server re-registers it | keeps the stash |
| `RECIPIENT_SERVER_UNREACHABLE` | nothing was presented at all | keeps the stash |
| `MESSAGE_TOO_LARGE` | the message is the problem | keeps the stash |

A refusal says that one capability was not one. It says nothing about the rest of
the stash, which has not been shown to anybody - so the message is offered again
with the next token rather than failed, and the activity row says which try it is
on: "Token rejected, 31 left…". The commonest cause is exactly that: another
device of the account spent that token first.

| Parameter | Value |
|---|---|
| Refused tokens one message walks through | 5 |

It stops there rather than walking the batch. Five refusals in a row is not a
stale token but a batch the far side no longer knows, and dialling through the
remaining two hundred would be many minutes of work for a message that is not
going to be taken. The message is then reported failed, with a resend control,
and this device asks its **own other devices** for a token
(`device.token-request`). The correspondent is not asked: a contact request is
the only tokenless path into a mailbox, and a top-up over it would turn one
narrow door into a channel anybody may knock on.

## Outbound addresses

One destination is held per recipient destination for the term stated above, then
discarded; a destination that has carried one correspondent's traffic is not
reassigned to another. Destinations are taken from the warm pool, which is
refilled immediately. Single-use destinations are retained for file transfers and
call media only.
