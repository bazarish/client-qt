// Bazarish project (c) 2026
package sh.bazari.client;

import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.PowerManager;

import java.nio.ByteBuffer;

// A call is spoken as a phone call: the voice stream, the earpiece unless the speaker is asked for.
public final class CallAudio {
    private static final int[] kTalkingDevices = {
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
        AudioDeviceInfo.TYPE_BLE_HEADSET,
        AudioDeviceInfo.TYPE_WIRED_HEADSET,
        AudioDeviceInfo.TYPE_USB_HEADSET,
        AudioDeviceInfo.TYPE_BUILTIN_EARPIECE,
    };
    private static final int[] kSpeakerDevices = {AudioDeviceInfo.TYPE_BUILTIN_SPEAKER};
    private static final int kBufferedFrames = 4;

    private static int users = 0;
    private static boolean speaker = false;
    private static PowerManager.WakeLock nearEar = null;

    private CallAudio() {}

    // Everything that sounds during a call - its tones and its voice - shares one route.
    private static synchronized void join() {
        AudioManager manager = Shade.app.getSystemService(AudioManager.class);
        if (users++ == 0) {
            manager.setMode(AudioManager.MODE_IN_COMMUNICATION);
        }
        route(manager);
    }

    private static synchronized void leave() {
        if (--users > 0) {
            return;
        }
        AudioManager manager = Shade.app.getSystemService(AudioManager.class);
        manager.clearCommunicationDevice();
        manager.setMode(AudioManager.MODE_NORMAL);
        holdNearEar(false);
    }

    public static synchronized void setSpeaker(boolean on) {
        speaker = on;
        if (users > 0) {
            route(Shade.app.getSystemService(AudioManager.class));
        }
    }

    // Near the ear the screen goes dark, so a cheek does not press the buttons.
    public static synchronized void holdNearEar(boolean on) {
        if (on && nearEar == null) {
            nearEar = Shade.app.getSystemService(PowerManager.class)
                .newWakeLock(PowerManager.PROXIMITY_SCREEN_OFF_WAKE_LOCK, "bazarish:ear");
            nearEar.acquire();
        } else if (!on && nearEar != null) {
            nearEar.release();
            nearEar = null;
        }
    }

    private static void route(AudioManager manager) {
        for (int type : speaker ? kSpeakerDevices : kTalkingDevices) {
            for (AudioDeviceInfo device : manager.getAvailableCommunicationDevices()) {
                if (device.getType() == type) {
                    if (!manager.setCommunicationDevice(device)) {
                        throw new IllegalStateException("the call could not be routed");
                    }
                    return;
                }
            }
        }
        throw new IllegalStateException("there is nothing to speak through");
    }

    public static final class Playback {
        private volatile AudioTrack track;

        public Playback(int sampleRate, int frameBytes) {
            join();
            int least = AudioTrack.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_16BIT);
            track = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build())
                .setAudioFormat(new AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(sampleRate)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .build())
                .setBufferSizeInBytes(Math.max(least, frameBytes * kBufferedFrames))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build();
            track.play();
        }

        public void write(ByteBuffer pcm, int bytes) {
            AudioTrack playing = track;
            if (playing == null) {
                return;
            }
            pcm.position(0).limit(bytes);
            if (playing.write(pcm, bytes, AudioTrack.WRITE_NON_BLOCKING) < 0) {
                throw new IllegalStateException("the call audio did not play");
            }
        }

        public void stop() {
            AudioTrack playing = track;
            track = null;
            if (playing != null) {
                playing.stop();
                playing.release();
                leave();
            }
        }
    }

    public static final class Capture {
        private volatile AudioRecord record;

        public Capture(int sampleRate, int frameBytes, boolean forCall) {
            int least = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT);
            // A call needs the voice-communication source's echo canceller; its noise
            // suppression would cut the quiet sounds out of a voice note.
            int source = forCall ? MediaRecorder.AudioSource.VOICE_COMMUNICATION
                : MediaRecorder.AudioSource.MIC;
            AudioRecord opened = new AudioRecord(source,
                sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT,
                Math.max(least, frameBytes * kBufferedFrames));
            if (opened.getState() != AudioRecord.STATE_INITIALIZED) {
                opened.release();
                throw new IllegalStateException("the microphone did not open");
            }
            opened.startRecording();
            record = opened;
        }

        public int read(ByteBuffer pcm, int bytes) {
            AudioRecord capturing = record;
            if (capturing == null) {
                return 0;
            }
            pcm.position(0).limit(bytes);
            return capturing.read(pcm, bytes, AudioRecord.READ_BLOCKING);
        }

        public void stop() {
            AudioRecord capturing = record;
            record = null;
            if (capturing != null) {
                capturing.stop();
                capturing.release();
            }
        }
    }
}
