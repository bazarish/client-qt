// Bazarish project (c) 2026
package org.bazarish.client;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.util.Log;

final class AudioFocus {
    private static final String TAG = "bazarish";

    private final AudioManager manager;
    private final AudioFocusRequest request;

    private AudioFocus(Context context, int gain, int usage, int content) {
        manager = context.getSystemService(AudioManager.class);
        request = new AudioFocusRequest.Builder(gain)
            .setAudioAttributes(new AudioAttributes.Builder()
                .setUsage(usage)
                .setContentType(content)
                .build())
            .build();
        if (manager.requestAudioFocus(request) != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            Log.w(TAG, "the system kept the audio focus: other sounds go on");
        }
    }

    static AudioFocus forVoicePlayback(Context context) {
        return new AudioFocus(context, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK,
            AudioAttributes.USAGE_MEDIA, AudioAttributes.CONTENT_TYPE_SPEECH);
    }

    static AudioFocus forVoiceRecording(Context context) {
        return new AudioFocus(context, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE,
            AudioAttributes.USAGE_MEDIA, AudioAttributes.CONTENT_TYPE_SPEECH);
    }

    static AudioFocus forCall(Context context) {
        return new AudioFocus(context, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT,
            AudioAttributes.USAGE_VOICE_COMMUNICATION, AudioAttributes.CONTENT_TYPE_SPEECH);
    }

    static AudioFocus forRinging(Context context) {
        return new AudioFocus(context, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT,
            AudioAttributes.USAGE_NOTIFICATION_RINGTONE, AudioAttributes.CONTENT_TYPE_SONIFICATION);
    }

    void release() {
        manager.abandonAudioFocusRequest(request);
    }
}
