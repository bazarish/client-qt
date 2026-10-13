# Groups and channels

## Scope

How this client shows a group or a channel hosted by a bot (`docs-main/Groups.md`):
what it keeps, what it draws, and what it lets a member and an admin do. The
protocol, the host and its limits are specified there.

## What makes a chat a group

A contact is a group while the messages it sends carry a `group` block. The
newest block, by its `at`, is kept per contact in the conversation database
(`group_state`), and the chat follows it at once:

- the header shows the member count under the name, `Members: N` for a group
  and `Subscribers: N` for a channel - a snapshot the host refreshes hourly;
- the composer disappears while the block says the member may not write, and
  the call button is hidden for a group;
- a message from that contact without a block and newer than the kept block
  turns the chat back into an ordinary one.

`author` is honoured only on a message with a block.

## What a post looks like

- Every incoming post carries the author's name above the text, in one fixed
  colour (`Theme.authorName`, 8.9:1 on the incoming bubble).
- Beside it is a circle with the first letter of the name. Its fill is one of
  eight muted colours, chosen by the SHA-256 of the name in NFC; each fill keeps
  the letter at 4.5:1 or better, which `TestLetterAvatar` checks.
- A message from the host without `author` is the host speaking to this member
  alone and is marked "Only you".
- A reply quote and the chat list name the author, not the group.

The author and the post's `contactable` flag are stored beside the message
(`group_posts`), keyed by the post id, which is the author's own id for every
member.

## Reactions

A member reacts as anywhere else. The count on a post is the host's
`group.reactions` figure, which leaves out the member's own reaction, plus the
member's own reaction from this device's table. Counts live in
`group_reaction_counts` and go with the post when it is deleted or the chat is
cleared.

## Member and admin actions

From a post's menu:

| Action | Shown to | Sends |
|---|---|---|
| Request contact | anyone, on a post whose author accepts requests | `bot.command contact <id>` |
| Exclude | an admin | `bot.command exclude <id>`, after a confirmation |
| Delete for everyone | an admin | `bot.command delete <id>`, after a confirmation |

Leaving and switching contact requests on or off are buttons in the host's
welcome message.

## Descriptors in text

A `bazarish://invite?...` link in any message, in any chat, is drawn as a plate
with the name it carries and a Write button that opens adding a contact with
that descriptor filled in. The rest of the text stays below.
