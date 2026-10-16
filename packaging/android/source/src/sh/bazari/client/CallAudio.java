// Bazarish project (c) 2026
package sh.bazari.client;

import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;

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

    private static volatile AudioTrack track = null;
    private static volatile AudioRecord record = null;
    private static volatile boolean speaker = false;

    private CallAudio() {}

    public static void startPlayback(int sampleRate, int frameBytes) {
        AudioManager manager = Bazarish.app.getSystemService(AudioManager.class);
        manager.setMode(AudioManager.MODE_IN_COMMUNICATION);
        speaker = false;
        route(manager);
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

    public static void write(ByteBuffer pcm, int bytes) {
        AudioTrack playing = track;
        if (playing == null) {
            return;
        }
        pcm.position(0).limit(bytes);
        if (playing.write(pcm, bytes, AudioTrack.WRITE_NON_BLOCKING) < 0) {
            throw new IllegalStateException("the call audio did not play");
        }
    }

    public static void stopPlayback() {
        AudioTrack playing = track;
        track = null;
        if (playing != null) {
            playing.stop();
            playing.release();
        }
        AudioManager manager = Bazarish.app.getSystemService(AudioManager.class);
        manager.clearCommunicationDevice();
        manager.setMode(AudioManager.MODE_NORMAL);
        speaker = false;
    }

    public static void startCapture(int sampleRate, int frameBytes) {
        int least = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT);
        // The voice-communication source brings the platform's echo canceller with it.
        record = new AudioRecord(MediaRecorder.AudioSource.VOICE_COMMUNICATION, sampleRate,
            AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT,
            Math.max(least, frameBytes * kBufferedFrames));
        if (record.getState() != AudioRecord.STATE_INITIALIZED) {
            record.release();
            record = null;
            throw new IllegalStateException("the microphone did not open for the call");
        }
        record.startRecording();
    }

    public static int read(ByteBuffer pcm, int bytes) {
        AudioRecord capturing = record;
        if (capturing == null) {
            return 0;
        }
        pcm.position(0).limit(bytes);
        return capturing.read(pcm, bytes, AudioRecord.READ_BLOCKING);
    }

    public static void stopCapture() {
        AudioRecord capturing = record;
        record = null;
        if (capturing != null) {
            capturing.stop();
            capturing.release();
        }
    }

    public static void setSpeaker(boolean on) {
        speaker = on;
        if (track != null) {
            route(Bazarish.app.getSystemService(AudioManager.class));
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
}
