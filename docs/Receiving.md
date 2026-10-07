# Incoming mail

## Scope

The protocol specifies that a mailbox holds a request open until something is
waiting for the caller, that it answers a delivery at once, and that it lists
what is waiting in arrival order (`docs-main/api/ClientApi.md`). This document
specifies how this client asks, what it does with the answer, and the values it
applies where the protocol names none.

## One mechanism

Mail is heard about in exactly one way: a request held open by the server. There
is no periodic poll of the mailbox, and no second mechanism behind the first - a
wait that ends in an error is asked again immediately, on the same connection the
loop already holds. A client that fell back to asking every few seconds would
turn a momentary transport failure into permanently late mail, which is what it
did before this was settled.

The same request is also what the account's connection state is drawn from: an
answer means the server is there, an error means it is not.

## Procedure

1. A loop of its own, on its own outbound destination, holds the wait. The
   session's own transport is left free for what the user is doing.
2. When the wait returns, the mailbox is read on the worker thread: one bounded
   pass, each item fetched and decrypted, each surfaced to the interface.
3. An item is acked only after the interface has durably stored it, so a crash
   between fetching and storing loses nothing.
4. The loop asks again once that pass has given back everything it took. A
   mailbox answers the next wait at once while it still holds what this device is
   carrying, so asking earlier would spin the loop through its own drain - and a
   pass that left items behind is exactly the case where answering at once is
   what continues it.

## Parameters

| Parameter | Value |
|---|---|
| Held-open request | 30 s asked; answered the moment mail lands |
| Items per pass | 5 |
| Local upkeep tick | 2 s |

The upkeep tick reads no mail. It carries what has no event of its own: contact
adds whose card was resolved off-thread, call ring and answer timeouts, the
echoes of this device's own sends to the account's other devices, and - each on
its own longer guard - the approval and delegation checks.

## Service messages

A conversation also holds rows that are not mail: the contact request with the
stages it goes through, a chat cleared on either side, a request the
correspondent agreed to. Such a row is stored as the sentence it names plus the
values that go into it - a correspondent's name, a count of attempts - and the
sentence is put together when the row is drawn. A conversation therefore reads in
the interface language in force, including the rows written before that language
was chosen, and so do the dates the conversation is divided by.

## Order and time

A conversation is ordered by the `sentAt` inside the sealed envelope, and by
nothing else (`docs-main/Messages.md`). The client does not order by when a
message reached it, and has nothing from a server to order by: a mailbox is told
the size and class of an envelope and never its contents. The arrival order the
mailbox lists in is what decides the order items are fetched, not where they end
up on screen.

A message that arrives long after it was written therefore appears where it was
written, which can be above the fold. The unread mark is kept over stored rows in
the order they were received rather than over their timestamps, so such a message
is unread whatever its own clock says.
