// Bazarish project (c) 2026
package sh.bazari.client;

import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.PowerManager;
import android.util.Log;

import java.nio.ByteBuffer;

// A voice note plays aloud; held to the ear it moves to the earpiece and the screen goes dark.
public final class VoicePlayback implements SensorEventListener {
    private static final String TAG = "bazarish";
    // A voice note already plays privately on these, and the ear changes nothing.
    private static final int[] kPersonalDevices = {
        AudioDeviceInfo.TYPE_WIRED_HEADSET,
        AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
        AudioDeviceInfo.TYPE_USB_HEADSET,
        AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
        AudioDeviceInfo.TYPE_BLE_HEADSET,
        AudioDeviceInfo.TYPE_HEARING_AID,
    };
    private static final int kBufferedFrames = 4;

    private final int sampleRate;
    private final int frameBytes;
    private final AudioManager manager;
    private final SensorManager sensors;
    private final Sensor proximity;
    private final PowerManager.WakeLock darkAtEar;
    private AudioTrack track;
    private boolean atEar = false;

    public VoicePlayback(int sampleRate, int frameBytes) {
        this.sampleRate = sampleRate;
        this.frameBytes = frameBytes;
        manager = Window.app.getSystemService(AudioManager.class);
        sensors = Window.app.getSystemService(SensorManager.class);
        proximity = sensors.getDefaultSensor(Sensor.TYPE_PROXIMITY);
        darkAtEar = Window.app.getSystemService(PowerManager.class)
            .newWakeLock(PowerManager.PROXIMITY_SCREEN_OFF_WAKE_LOCK, "bazarish:voice");
        track = open(AudioAttributes.USAGE_MEDIA);
        if (proximity != null
            && !sensors.registerListener(this, proximity, SensorManager.SENSOR_DELAY_NORMAL)) {
            Log.w(TAG, "the proximity sensor is not granted: voice notes stay on the speaker");
        }
    }

    // The voice-communication usage is what the platform routes to the earpiece.
    private AudioTrack open(int usage) {
        int least = AudioTrack.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT);
        AudioTrack opened = new AudioTrack.Builder()
            .setAudioAttributes(new AudioAttributes.Builder()
                .setUsage(usage)
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
        opened.play();
        return opened;
    }

    private boolean playsPrivately() {
        for (AudioDeviceInfo device : manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
            for (int type : kPersonalDevices) {
                if (device.getType() == type) {
                    return true;
                }
            }
        }
        return false;
    }

    public synchronized void write(ByteBuffer pcm, int bytes) {
        if (track == null) {
            return;
        }
        pcm.position(0).limit(bytes);
        if (track.write(pcm, bytes, AudioTrack.WRITE_NON_BLOCKING) < 0) {
            throw new IllegalStateException("the voice note did not play");
        }
    }

    @Override
    public synchronized void onSensorChanged(SensorEvent event) {
        if (track == null) {
            return;
        }
        boolean near = event.values[0] < proximity.getMaximumRange() && !playsPrivately();
        if (near == atEar) {
            return;
        }
        atEar = near;
        track.pause();
        track.flush();
        track.release();
        track = open(near ? AudioAttributes.USAGE_VOICE_COMMUNICATION : AudioAttributes.USAGE_MEDIA);
        if (near) {
            darkAtEar.acquire();
        } else {
            darkAtEar.release();
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {}

    public synchronized void stop() {
        if (track == null) {
            return;
        }
        sensors.unregisterListener(this);
        track.stop();
        track.release();
        track = null;
        if (atEar) {
            atEar = false;
            darkAtEar.release(PowerManager.RELEASE_FLAG_WAIT_FOR_NO_PROXIMITY);
        }
    }
}
