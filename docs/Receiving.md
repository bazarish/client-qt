# Incoming mail

## Scope

The protocol specifies that a mailbox holds a request open until something is
waiting for the caller, that it answers a delivery at once, and that it lists
what is waiting in arrival order (`docs-main/api/ClientApi.md`). This document
specifies how this client asks, what it does with the answer, and the values it
applies where the protocol names none.

## One mechanism

Mail is heard about in exactly one way: a request held open by the server. A
wait that ends in an error is asked again immediately, on the same connection the
loop already holds.

The same request is also what the account's connection state is drawn from: an
answer means the server is there, an error means it is not.

It reports failures, not absences, so **whatever stops that loop on purpose has
to report the loss itself**. Rebuilding the I2P links - which is what moving the
tunnel-length slider does - tears the loop down, releases the links and starts a
new loop that cannot answer until fresh tunnels are up; the state is set to
unreachable at the moment the links go, and the first answer from the new loop
puts it back.

## Procedure

1. A loop of its own, on its own outbound destination, holds the wait. The
   session's own transport is left free for what the user is doing.
2. When the wait returns, the mailbox is read on the worker thread: one bounded
   pass, each item fetched and decrypted, each surfaced to the interface.
3. An item is acked only after the core has stored it. A store that fails leaves
   the item on the server and stops receiving for that account with the
   database's error, so a crash or a full disk between fetching and storing loses
   nothing.
4. The loop asks again once that pass has given back everything it took. A
   mailbox answers the next wait at once while it still holds what this device is
   carrying, and a pass that left items behind is exactly the case where
   answering at once is what continues it.
5. A wait that fails, other than as in step 6, is asked again on a cause, never
   on a period of its own:

   | Cause | When |
   |---|---|
   | A held wait broke | At once, one time per answered wait that had a cause of its own: the stream went, a new one is the answer |
   | The link came back | The gateway session rejoined, or the device moved to another network |
   | Our server answered | Another request of this account reached the server, a letter to the account's own mailbox among them |
   | This device was registered again | After a wait the server refused as unregistered |
   | The facade published a newer lease set | Looked up past the cache once after each failed wait; the stand-off of the link is lifted with it |
   | The lease set of the failed wait has expired | `kStaleLeaseSetBound` after the failure, when nothing above came first: a dial then can only use a fresh lease set |

6. A wait the facade answered with 502 `UPSTREAM_UNAVAILABLE` reached the facade
   but not the server behind it, and nothing on the client hears of the server's
   return. It is asked again after 1 s, then 5 s, then 10 s and every 10 s after
   that, until the facade answers anything else; any other answer starts the
   sequence over at 1 s. The link came back, our server answered and this device
   was registered again still end a pause early; no lease set is looked up, since
   the facade answered. From the first such answer to the first other one the
   account shows **No tunnel to the server**.
7. A wait that fails while the device has no network waits for the network
   alone: no lease set ages meanwhile, so the network coming back is its only
   cause.

   A closed or locked account stops the loop; it asks nothing.

## When the network goes

The platform says when the network is gone and when it is back: on Android the
default network being lost or losing its internet capability, on the desktop the
system reporting no connection at all. A network that reaches only the local
site still counts as a network, since a gateway may be on it.

When it goes, the gateway's sockets are closed, every gateway call that is
waiting fails at once and no stream is dialled until the network is back, so
nothing piles up behind a dead socket. When it comes back, the mail wait is
asked first. What waits in the outbox for any recipient goes once our server has
answered - that wait or any other request - so the link is known to be up end to
end, and not before (`Sending.md`).

## What the connection strip says

While an account is switched on but its mail wait is not answered, the strip at
the top names the state and, beside it, why the server is out of reach. The
reason is the one the last failed wait gave:

| Shown | When |
|---|---|
| **No network** (in place of *Connecting*) | The device has no network |
| No connection to the I2P network | The wait failed before the router had a link to I2P |
| No connection to the external gateway | The gateway does not answer, or the session with it is not ready |
| The gateway refused a link to the server | The gateway answered and refused the stream |
| The I2P tunnels are being built | The router is up and the tunnels of the wait are not |
| Could not connect to the server | The facade's address was dialled and did not answer |
| No tunnel to the server | The facade answered 502 `UPSTREAM_UNAVAILABLE`: it has no server behind it |
| The server refused the request | The server answered with a refusal |

Before any wait has failed there is no reason, and the strip says only
*Connecting*.

## Parameters

| Parameter | Value |
|---|---|
| Held-open request | No time limit; answered the moment mail lands |
| A failed wait asked again | on a cause (step 5); without one, after 11 min (lease lifetime 10 min and 1 min of clock skew) |
| A wait the facade answered with 502 asked again | after 1 s, 5 s, 10 s, then every 10 s (step 6) |
| Items per pass | 5 |
| Local upkeep tick | 2 s |

The upkeep tick reads no mail. It carries what has no event of its own: contact
adds whose card was resolved off-thread, call ring and answer timeouts, the
echoes of this device's own sends to the account's other devices, and - each on
its own longer guard - the approval and delegation checks.

## Service messages

A conversation also holds rows that are not mail: the contact request with the
stages it goes through, a chat cleared on either side, a request the
correspondent agreed to, and - after this account has changed its address - a
notice that its new contact details did not reach this correspondent, with a
**Try again** beside it. That one is written per correspondent, in their own
chat, and is removed when the update lands; the queue behind it survives a
restart and is also tried on its own whenever the account comes back online. Such a row is stored as the sentence it names plus the
values that go into it - a correspondent's name, a count of attempts - and the
sentence is put together when the row is drawn. A conversation therefore reads in
the interface language in force, including the rows written before that language
was chosen, and so do the dates the conversation is divided by.

## Order and time

A conversation is ordered by the `sentAt` inside the sealed envelope, except that
a message from the other side never goes above what this side had written in the
chat before it arrived (`docs-main/Messages.md`): its order key is raised to one
millisecond after the newest own row, and the time it shows stays its `sentAt`. A
row written on another device of the same account is not raised. The arrival
order the mailbox lists in decides only the order items are fetched in.

A message that arrives long after it was written therefore appears where it was
written among the other side's messages, but under anything written here before it
came, which can still be above the fold. The unread mark is kept over stored rows in
the order they were received, so such a message is unread whatever its own clock
says.
