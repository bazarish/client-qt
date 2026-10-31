# Message formatting

## Scope

A message body travels as plain text. The protocol specifies no formatting, and
nothing in this document changes what is sent: the markers below are characters
in the message, and a client that does not know them shows them as typed. What
this document specifies is how this client draws a body it has received, and what
the clickable forms do when they are pressed.

The list is closed. What is not named here is text.

## The forms

| Written | Drawn as |
|---|---|
| `*text*` | italic |
| `**text**` | bold |
| `~~text~~` | struck through |
| ` ```text``` ` | a block kept exactly as written, copied when pressed |
| `!!text!!` | a marked word that sends `text` to this chat when pressed |
| `!name`, `!name-word` | a marked alias that opens the add-a-contact form for it |
| `http://...`, `https://...` | an underlined address, followed only after a warning |

Bold, italic and struck-through are drawn where they stand. The clickable forms
are drawn on a ground of their own (the block, the offer to send, the name) or
underlined (the address), and the pointer changes over them.

## Rules

A marker opens only at the edge of a word, against text rather than against a
space, and closes the same way. `2*3*4` is arithmetic and stays as typed; so does
a marker inside a word.

A span never crosses a line - with one exception, the block, which exists to keep
the lines it was given. An unclosed marker therefore costs one line rather than
the rest of the message, and stays on screen as the characters it is; an unclosed
block marker is the three characters it is.

Styles do not nest. `**bold *and* more**` is bold from end to end, with the inner
markers drawn as characters. A style does carry the clickable forms inside it, so
`**press !!/help!!**` is a bold line with a pressable `/help` in it.

Nothing between `!!` and `!!` is read as markup: what is written there is what is
sent, character for character. The same holds between ` ``` ` and ` ``` `: what
is written there is what is drawn and what is copied, spacing and line breaks
included. A tab is drawn four spaces wide, a rich text document having no tab
stops of its own; the character copied back out is still a tab.

An alias is `!` followed by Latin letters and digits, at most 16 of them, and
optionally by `-` and a word of at most 16 more - the grammar the add-a-contact
form resolves. `!bob.` marks `bob` and leaves the stop, and `!bob-` leaves the
hyphen; `!bob_smith` and `!bob-a-b` are left alone entirely, because marking
`bob` out of them would open the form on a name nobody typed. `Wow!Great` is an
exclamation, not a name.

An address ends at the first space. A full stop, comma or closing bracket that
the address did not open is punctuation and is left out of it. Only `http` and
`https` are recognised; every other scheme is text.

There is no escape character. A marker that does not pair off is drawn as itself,
which is what makes one unnecessary.

## What a press does

**`!!text!!`** sends `text` to the open chat as an ordinary message, at once. It
is a message like any other: it appears in the conversation, it is delivered, and
it can fail like any other. The composer is not touched - a draft being written
and a reply being aimed at a message both stay where they are. This is how a bot
offers a command inside a sentence; an inline keyboard is the other way, and
differs in that a keyboard press sends `bot.callback` with a reference to the
message it was attached to and shows nothing in the conversation.

**An entry of a bot's menu** sends `/command` the same way, as an ordinary
message to the open chat. The menu is the newest `bot.menu` that contact sent
(`docs-main` `Bots.md`). While a chat has one, a "Bot menu" strip stands above
its messages, the on-screen keyboard up or not; opening the menu from it puts
the keyboard away.

**A block** goes to the clipboard, as it was written. Nothing is sent, and
nothing leaves the machine; the bubble says "Copied to clipboard" underneath for
a moment, because a clipboard is somewhere the user cannot see and a press that
changes nothing on screen reads as a press that did nothing.

**`!name`** opens the add-a-contact form on its alias page with the alias
filled in. Nothing is sent: the introduction and the request are still the user's
to write and press.

**An address** opens the warning first. It shows the address and says that the
site will probably learn this machine's IP address: the client does not fetch it,
the browser goes there directly. On a desktop the address also
shows while the pointer rests on it; a phone has no such pause, so the warning is
where it is read. Answering "Open" hands it to the desktop, and a desktop with nothing registered
for `http` is reported rather than passed over in silence.

There is no way to give an address a label that differs from where it goes: the
markup has no such form, so what the bubble shows is what would be opened.

## The buttons over the composer

While something is selected in the input, a row of buttons appears above it: one
per form, showing the markers it puts in and what they do (`**` bold, `*` italic,
`~~` strike, ` ``` ` block, `!!` command). Pressing one wraps the selection in
those characters and keeps the selection, so a second form can be applied without
reaching for the mouse again.

They are a way of typing, not a second syntax: what is sent is the text with the
markers in it, indistinguishable from the same message typed by hand. There is no
button for an address, because an address needs no markers - it is recognised on
the receiving side by what it is.

## Where a body is not drawn as markup

The chat list preview, the reply banner over the composer, the reply quote inside
a bubble and a search hit all show the body with the markers taken out and
nothing pressable in it: one line of what was said, not a rendering of it.

A contact request from someone who is not a contact yet is shown the same way.
Their words are drawn; their markup is not, and nothing in it can be pressed.

Editing one's own message puts the body back in the composer as it was written,
markers and all.

## Copying

Selecting part of a bubble and copying takes the text as drawn, without the
markers. "Copy all" in the message menu takes the body as it was written, which
is the form that can be sent again.

## Why the client builds the document

A bubble is a rich text document, and the document is built here from the message
rather than handed the message. Every character a correspondent sent is escaped
first, and the only tags in the document are the ones this client put there.

A body passed through as rich text would let a correspondent name a picture in
it, and a picture named in a document is fetched as the document draws - from
this machine, over the ordinary internet, before anything has been pressed. That
is the same disclosure the link warning exists to prevent, minus the warning.

## Not supported

Headings, lists, quotes, tables, images, links with a label of their own,
colours, syntax highlighting inside a block, and any nesting of styles. None of
them are planned here: the list at the top is what a message is allowed to say
about itself.
