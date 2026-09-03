# Sounds

## Scope

The client makes two sounds of its own, and carries both. This document states
where a recording of the user's own goes instead, what each one has to be, and
what the ringing call window's pulse is taken from.

## The two

| Sound | When it is made | Replaced by |
|---|---|---|
| Notification | A message arrives that the user is not already looking at | `notify.wav` |
| Ringtone | A call is ringing, repeated until it is answered, declined or given up on | `ringtone.wav` |

A call owns the sound while it rings: nothing announces a message over it.

Arrivals are announced without being smeared together: while what was announced is
still unread, two sounds are kept at least **half again the length of the sound in
use** apart, so a flood of messages is heard as a flood rather than as one long
noise made of sounds starting over each other. Reading what was announced clears
the hold, and the next arrival is heard as it comes. The spacing follows the file:
a longer recording of the user's own spaces itself further apart without anything
being configured. Only the sound is rationed - every message still shows its
popup.

## Where a replacement goes

At the root of the installation - the directory that holds `accounts/`,
`settings.json` and the embedded router's state:

| Where the client runs | Root |
|---|---|
| Linux | `~/.local/share/bazarish` |
| Windows | `%APPDATA%\Bazarish` |
| Portable: a file named `.bazarish.portable` beside the executable | `<executable's directory>/bazarish_data` |

Nothing beside the executable is read in any other case, and `BAZARISH_ACCOUNTS_DIR`
moves the root with the accounts directory it names.

The two names above are the only ones looked for, and `.wav` is the only format:
a sound is a known file rather than a directory to be searched through. Both are
consulted every time the sound is made, so a file put there is used without
restarting and taking it away brings the carried one back.

## What the ringtone has to be

**16-bit mono PCM, at most 8 MiB** - about 87 seconds at 48 kHz.

The client reads the ringtone's samples itself rather than handing the file to a
player, because the call window pulses with it: that needs the loudness of what
is being heard at this instant, which a player will not tell. The loudness is
measured over 20 ms frames, taken against the track's own loudest moment, and
published about thirty times a second. The position in the track is counted in
samples that have actually been played, so a track of any length takes the pulse
back to its start exactly where the sound repeats.

That is also why the format is narrow. A track that is not it - stereo, another
depth, another format under a `.wav` name, past the size above, or silent - is
refused, the reason is written to the log, and the carried track rings instead. A
call is not left silent by a choice of track.

The notification sound is handed to the media backend and is not read this way,
so it is any WAV the backend will play. One that will not play says so in the
log rather than passing as silence.
