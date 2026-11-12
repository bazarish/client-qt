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
5. A wait that fails is asked again on a cause, never on a period of its own:

   | Cause | When |
   |---|---|
   | A held wait broke | At once, one time per answered wait that had a cause of its own: the stream went, a new one is the answer |
   | The link came back | The gateway session rejoined, or the device moved to another network |
   | Our server answered | Another request of this account reached the server, a letter to the account's own mailbox among them |
   | This device was registered again | After a wait the server refused as unregistered |
   | The facade published a newer lease set | Looked up past the cache once after each failed wait; the stand-off of the link is lifted with it |
   | The lease set of the failed wait has expired | `kStaleLeaseSetBound` after the failure, when nothing above came first: a dial then can only use a fresh lease set |

   A closed or locked account stops the loop; it asks nothing.

## Parameters

| Parameter | Value |
|---|---|
| Held-open request | No time limit; answered the moment mail lands |
| A failed wait asked again | on a cause (step 5); without one, after 11 min (lease lifetime 10 min and 1 min of clock skew) |
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
