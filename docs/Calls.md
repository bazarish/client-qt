# Calls

## Scope

The protocol specifies what a call is: the signalling messages, the media
transport (I2P RAW datagrams to a one-time b33 destination), the per-call key and
the ring timeouts (`docs-main/api/Calls.md`). This document states how this client
carries them, and the choices the protocol leaves to it.

## What carries the media

| Piece | Where |
|---|---|
| The RAW datagram endpoint | `bazarish::i2p::Endpoint`, over whichever transport is in force |
| Opus, at the call format | `AudioCodec` |
| Devices, and the synthetic backends a headless test uses | `AudioIo`, Qt Multimedia's `QAudioSource` / `QAudioSink` |
| The media engine: one capture thread, one receive thread | `CallMedia` |
| The per-call transport | `I2pCallTransport` |
| Signalling and the state machine | `Session` (`startAudioCall`, `acceptCall`, `declineCall`, `endCall`, `setCallMuted`, `currentCall`) |
| The window | `CallScreen.qml`, `IncomingCallWindow.qml` |

## Datagrams to a blinded address

The protocol requires media datagrams addressed to a b33, and a call's media
destination is published like any other. Routing a datagram to a blinded address
means looking the leaseset up first, which a router does not do for a plain
identity hash.

Where that lookup happens follows the transport. The embedded engine does it in
this client's own code, which resolves the blinded address once and remembers it
for the length of the call - a call is a stream of media, not one message. A
private gateway does the same, because it runs the same code. Over **SAM** the
router does it, and i2pd has done so since **2.61.0**, which is the version this
project requires anyway; against an older one a datagram to a b33 is refused as
an invalid destination and the call connects and signals with no audio.

File transfer is not affected either way: it opens a stream, and SAM has
connected a stream to a b33 since i2pd 2.38.0.

## A lane of its own

The embedded router pins every destination to one of a few single-threaded lanes
for its lifetime, so all of that destination's handlers are serialised. A bulk
transfer sharing a lane with a call is a call that stutters while the file moves,
even though the tunnels and the link are free.

The first lane is therefore kept for real-time media and handed out to nothing
else: a media destination asks for it (`EndpointConfig::realtime`), and the round
robin that places every other destination starts at the second lane. A call has
one media destination at a time, so that lane carries one destination for the
length of the call. With a single lane there is nothing to keep apart and
everything shares it.

## What a call leaves behind

Nothing in the conversation. A call is not correspondence: no line in the
transcript and no text in the chat list. What it leaves is the window it put on
the screen while it rang, and - for a call this side did not get to - the failure
tone. The media addresses and the datagram counts at the end of a call go to the
log, which is where a call with no sound has to be diagnosed from.
