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
- A post with `fromHost` is the group itself speaking to everyone: no author
  line, no circle, no mark.
- A message from the host with neither `author` nor `fromHost` is the host
  speaking to this member alone and is marked "Only you".
- A reply quote and the chat list name the author, not the group.

The author and the post's `contactable` flag are stored beside the message
(`group_posts`), keyed by the post id, which is the author's own id for every
member. A host's post is a row with an empty author; no row at all is "Only you".

## Reactions

A member reacts as anywhere else. The count on a post is the host's
`group.reactions` figure, which leaves out the member's own reaction, plus the
member's own reaction from this device's table. Counts live in
`group_reaction_counts` and go with the post when it is deleted or the chat is
cleared.

## Member list

In a `group` the client keeps the host's `group.members` letters in the
conversation database (`group_members`, with the list state and the own name in
`group_rosters`). The core asks the host with `group.members.ask` once per run
of the client for a group with no list, when the first message with a `group`
block comes in, and again whenever a list arrives broken or a change leaves a
count other than `total`.

- Chat info shows a "Member list" button while the list is held. It is hidden
  while the list is not known yet and in a channel (`op: none`).
- The button opens a window with the names in the alphabetical order of the
  interface language (`QCollator`, numbers by value). The search button filters
  this local list by any part of a name or of its mention form.
- Chat info shows no device list for a bot or a group: they have one device.

## Mentions

The composer, the bubbles and the chat list follow `docs-main/Groups.md`,
Mentions, with the parser in common (`Mentions.hpp`).

- Typing `@` and at least one character after white space, an opening bracket
  or quote, or at the start of the text, shows up to 5 member names that start
  with the typed characters, compared case-folded and with spaces as `_`. The
  own name is not offered.
- A chosen name is written as `@` and its mention form, followed by a space. On
  a desktop Tab writes the name when only one is offered, and a click on a name
  writes it; on a phone a name is chosen from the list.
- A mention of a member in a post's text is drawn in `Theme.mentionInk`, bold.
  Positions come from UTF-8 offsets and are converted to the text's UTF-16
  positions (`MentionText.cpp`).
- A post that mentions this member, or replies to one of their posts, has the
  `Theme.bubbleMention` fill and a 1 px `Theme.mentionEdge` border. Every text
  colour drawn in a bubble keeps 4.5:1 on that fill in both themes.
- A group with unread mentions shows an `@` mark beside the unread count in the
  chat list. A tap on the mark opens the chat at the first unread mention,
  centred and highlighted; a tap elsewhere on the row opens it as before.
- A post that mentions this member wakes a notification even when the chat's
  notifications are off. The application's own notification switch still
  applies.

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
