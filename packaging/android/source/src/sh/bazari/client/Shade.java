// Bazarish project (c) 2026
package sh.bazari.client;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Person;
import android.content.ComponentName;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.os.SystemClock;
import android.os.VibrationAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.os.ResultReceiver;
import android.service.notification.StatusBarNotification;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

// The core's side of Android: the shade, the notices and the ring, all in the :core process.
public final class Shade {
    static final String CHANNEL_PRESENCE = "status";
    static final String CHANNEL_MESSAGES = "messages";
    static final String CHANNEL_REACTIONS = "reactions";
    static final String CHANNEL_CALLS = "calls";
    static final String EXTRA_ACCOUNT = "sh.bazari.client.account";
    static final String EXTRA_PEER = "sh.bazari.client.peer";
    static final String EXTRA_LISTENER = "sh.bazari.client.listener";
    static final String ACTION_ANSWER = "sh.bazari.client.ANSWER";
    static final String ACTION_RING = "sh.bazari.client.RING";
    static final String ACTION_TOGGLE = "sh.bazari.client.TOGGLE_NOTIFICATIONS";
    static final String ACTION_QUIT = "sh.bazari.client.QUIT";
    static final String ACTION_DECLINE = "sh.bazari.client.DECLINE";
    // Not exported: what a notice asks of the window cannot come from another app.
    static final String NOTICE_ENTRY = "sh.bazari.client.NoticeEntry";
    static final int PRESENCE_ID = 1;
    private static final int CALL_ID = 2;
    private static final int MESSAGE_ID = 3;
    private static final int ANSWER_REQUEST = 1;
    private static final int RING_REQUEST = 2;
    private static final long WAKE_MILLIS = 3000;
    private static MediaPlayer ring = null;
    private static Vibrator buzzing = null;
    private static final long RING_PULSE_MILLIS = 800;
    private static final long RING_PAUSE_MILLIS = 800;
    private static final int REPEAT_FROM_START = 0;
    private static final int NO_REPEAT = -1;
    private static final long MESSAGE_PULSE_MILLIS = 250;
    private static final long MESSAGE_GAP_MILLIS = 250;
    private static long lastBuzzAt = 0;

    static Context app;
    static String turnOff = "";
    static String turnOn = "";
    static String quit = "";
    static String line = "";
    static boolean notificationsOn = true;
    private static boolean listening = false;
    private static final List<ResultReceiver> waiting = new ArrayList<>();
    private static boolean nativesReady = false;
    private static final List<String> heldActions = new ArrayList<>();

    static native void notificationsToggled();
    static native void quitRequested();
    static native void callDeclined();
    static native void networkChanged();

    private Shade() {}

    // A window waits for this before it connects: the core answers once its socket listens.
    static synchronized void whenListening(ResultReceiver receiver) {
        if (listening) {
            receiver.send(0, null);
            return;
        }
        waiting.add(receiver);
    }

    public static synchronized void listening() {
        listening = true;
        for (ResultReceiver receiver : waiting) {
            receiver.send(0, null);
        }
        waiting.clear();
    }

    // A button of a notice can start this process anew, before Qt has said what it answers to.
    static synchronized void act(String action) {
        if (!nativesReady) {
            heldActions.add(action);
            return;
        }
        run(action);
    }

    public static synchronized void nativesRegistered() {
        nativesReady = true;
        for (String action : heldActions) {
            run(action);
        }
        heldActions.clear();
        app.getSystemService(ConnectivityManager.class).registerDefaultNetworkCallback(
            new ConnectivityManager.NetworkCallback() {
                @Override
                public void onAvailable(Network network) {
                    networkChanged();
                }
            });
    }

    private static void run(String action) {
        if (ACTION_TOGGLE.equals(action)) {
            notificationsToggled();
        } else if (ACTION_QUIT.equals(action)) {
            quitRequested();
        } else if (ACTION_DECLINE.equals(action)) {
            callDeclined();
        }
    }

    public static void show(String presence, String messages, String reactions, String calls,
            String turnOffText, String turnOnText, String quitText, boolean notifications,
            String presenceLine) {
        turnOff = turnOffText;
        turnOn = turnOnText;
        quit = quitText;
        notificationsOn = notifications;
        line = presenceLine;
        NotificationManager manager = app.getSystemService(NotificationManager.class);
        presenceChannel(app, presence);
        NotificationChannel messagesChannel = sounding(CHANNEL_MESSAGES, messages,
            NotificationManager.IMPORTANCE_HIGH, "notify");
        // Felt as the phone's own setting for notification vibration decides.
        messagesChannel.enableVibration(true);
        manager.createNotificationChannel(messagesChannel);
        NotificationChannel reactionsChannel = new NotificationChannel(
            CHANNEL_REACTIONS, reactions, NotificationManager.IMPORTANCE_DEFAULT);
        // A channel's sound turns into vibration in vibrate mode: the chime is played here instead.
        reactionsChannel.setSound(null, null);
        reactionsChannel.enableVibration(false);
        manager.createNotificationChannel(reactionsChannel);
        NotificationChannel callsChannel = new NotificationChannel(
            CHANNEL_CALLS, calls, NotificationManager.IMPORTANCE_HIGH);
        // The ring is played on the ring stream for as long as the call rings.
        callsChannel.setSound(null, null);
        manager.createNotificationChannel(callsChannel);
        manager.notify(PRESENCE_ID, BazarishService.presence(app));
    }

    // Low importance is what Android draws collapsed until it is pulled open.
    static void presenceChannel(Context context, String name) {
        NotificationChannel channel = new NotificationChannel(
            CHANNEL_PRESENCE, name, NotificationManager.IMPORTANCE_LOW);
        channel.setShowBadge(false);
        context.getSystemService(NotificationManager.class).createNotificationChannel(channel);
    }

    public static void notifyMessage(String account, String peer, String title, String body,
            boolean reaction) {
        Notification shown = new Notification.Builder(
                app, reaction ? CHANNEL_REACTIONS : CHANNEL_MESSAGES)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(title)
            .setContentText(body)
            .setStyle(new Notification.BigTextStyle().bigText(body))
            .setCategory(Notification.CATEGORY_MESSAGE)
            .setContentIntent(opening(account, peer))
            .setAutoCancel(true)
            .build();
        if (reaction) {
            chimeForReaction();
        }
        app.getSystemService(NotificationManager.class)
            .notify(account + "/" + peer, MESSAGE_ID, shown);
        wakeScreen();
    }

    // A message lights a sleeping screen for a moment, as a phone call's would.
    @SuppressWarnings("deprecation")
    private static void wakeScreen() {
        PowerManager power = app.getSystemService(PowerManager.class);
        if (power.isInteractive()) {
            return;
        }
        power.newWakeLock(PowerManager.SCREEN_DIM_WAKE_LOCK | PowerManager.ACQUIRE_CAUSES_WAKEUP,
            "bazarish:message").acquire(WAKE_MILLIS);
    }

    public static void notifyCall(String account, String peer, String title, String body) {
        Person caller = new Person.Builder().setName(title).setImportant(true).build();
        PendingIntent answer = PendingIntent.getActivity(app, ANSWER_REQUEST,
            notice().setAction(ACTION_ANSWER)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent ringing = PendingIntent.getActivity(app, RING_REQUEST,
            notice().setAction(ACTION_RING)
                .putExtra(EXTRA_ACCOUNT, account)
                .putExtra(EXTRA_PEER, peer)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification shown = new Notification.Builder(app, CHANNEL_CALLS)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentText(body)
            .setStyle(Notification.CallStyle.forIncomingCall(
                caller, BazarishService.declining(app), answer))
            .setCategory(Notification.CATEGORY_CALL)
            .setContentIntent(ringing)
            .setFullScreenIntent(ringing, true)
            .setOngoing(true)
            .build();
        app.getSystemService(NotificationManager.class).notify(CALL_ID, shown);
    }

    // Android keeps the microphone open in the background only for a microphone service.
    public static void callTalking(boolean on) {
        BazarishService.talking = on;
        BazarishService service = BazarishService.running.get();
        if (service != null) {
            service.comeForward();
        }
    }

    public static void startRinging() {
        if (ring != null) {
            return;
        }
        ring = new MediaPlayer();
        ring.setAudioAttributes(new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_NOTIFICATION_RINGTONE)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build());
        ring.setLooping(true);
        try {
            ring.setDataSource(app, rawSound("ringtone"));
            ring.prepare();
        } catch (IOException e) {
            throw new IllegalStateException("the ringtone did not load", e);
        }
        ring.start();
        vibrateForRinging();
    }

    // Rung as a ringtone: the phone's own setting for vibrating on calls, and its ringer
    // mode, decide whether this is felt.
    private static void vibrateForRinging() {
        Vibrator vibrator = app.getSystemService(VibratorManager.class).getDefaultVibrator();
        if (!vibrator.hasVibrator()) {
            return;
        }
        VibrationEffect pulse = VibrationEffect.createWaveform(
            new long[] {0, RING_PULSE_MILLIS, RING_PAUSE_MILLIS}, REPEAT_FROM_START);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            vibrator.vibrate(
                pulse, VibrationAttributes.createForUsage(VibrationAttributes.USAGE_RINGTONE));
        } else {
            vibrator.vibrate(pulse, new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_NOTIFICATION_RINGTONE)
                .build());
        }
        buzzing = vibrator;
    }

    // Asked as a notice on this channel would be: the user's setting for it and Do not disturb.
    private static boolean mayInterrupt(String channelId) {
        NotificationManager manager = app.getSystemService(NotificationManager.class);
        return manager.areNotificationsEnabled()
            && manager.getNotificationChannel(channelId).getImportance()
                >= NotificationManager.IMPORTANCE_DEFAULT
            && manager.getCurrentInterruptionFilter() == NotificationManager.INTERRUPTION_FILTER_ALL;
    }

    // A message that lands while the window is in front is felt instead of shown.
    public static void buzzForMessage() {
        NotificationManager manager = app.getSystemService(NotificationManager.class);
        if (!mayInterrupt(CHANNEL_MESSAGES)
                || !manager.getNotificationChannel(CHANNEL_MESSAGES).shouldVibrate()) {
            return;
        }
        long buzzMillis = MESSAGE_PULSE_MILLIS + MESSAGE_GAP_MILLIS + MESSAGE_PULSE_MILLIS;
        long now = SystemClock.elapsedRealtime();
        if (now - lastBuzzAt < buzzMillis) {
            return;
        }
        lastBuzzAt = now;
        Vibrator vibrator = app.getSystemService(VibratorManager.class).getDefaultVibrator();
        VibrationEffect pulses = VibrationEffect.createWaveform(
            new long[] {0, MESSAGE_PULSE_MILLIS, MESSAGE_GAP_MILLIS, MESSAGE_PULSE_MILLIS}, NO_REPEAT);
        // Notification usage: the phone's notification vibration setting and ringer mode apply.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            vibrator.vibrate(
                pulses, VibrationAttributes.createForUsage(VibrationAttributes.USAGE_NOTIFICATION));
        } else {
            vibrator.vibrate(pulses, new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_NOTIFICATION)
                .build());
        }
    }

    // Played on the notification stream, which the ringer mode silences.
    private static void chimeForReaction() {
        if (!mayInterrupt(CHANNEL_REACTIONS)) {
            return;
        }
        MediaPlayer chime = new MediaPlayer();
        chime.setAudioAttributes(new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_NOTIFICATION)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build());
        chime.setOnCompletionListener(MediaPlayer::release);
        try {
            chime.setDataSource(app, rawSound("reaction"));
            chime.prepare();
        } catch (IOException e) {
            chime.release();
            throw new IllegalStateException("the reaction sound did not load", e);
        }
        chime.start();
    }

    public static void stopRinging() {
        if (ring == null) {
            return;
        }
        ring.stop();
        ring.release();
        ring = null;
        if (buzzing != null) {
            buzzing.cancel();
            buzzing = null;
        }
    }

    public static void cancelCall() {
        app.getSystemService(NotificationManager.class).cancel(CALL_ID);
    }

    static void clearMessages(Context context) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        for (StatusBarNotification shown : manager.getActiveNotifications()) {
            if (shown.getId() == MESSAGE_ID) {
                manager.cancel(shown.getTag(), MESSAGE_ID);
            }
        }
    }

    public static void stop() {
        app.getSystemService(NotificationManager.class).cancelAll();
        app.stopService(new Intent(app, BazarishService.class));
    }

    // By name: a channel keeps its sound for good, and a resource number moves between builds.
    private static Uri rawSound(String name) {
        return Uri.parse(ContentResolver.SCHEME_ANDROID_RESOURCE + "://" + app.getPackageName()
            + "/raw/" + name);
    }

    private static NotificationChannel sounding(String id, String name, int importance,
            String sound) {
        NotificationChannel channel = new NotificationChannel(id, name, importance);
        channel.setSound(rawSound(sound),
            new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_NOTIFICATION)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build());
        return channel;
    }

    private static Intent notice() {
        return new Intent().setComponent(new ComponentName(app, NOTICE_ENTRY));
    }

    private static PendingIntent opening(String account, String peer) {
        Intent open = notice()
            .putExtra(EXTRA_ACCOUNT, account)
            .putExtra(EXTRA_PEER, peer)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        return PendingIntent.getActivity(app, (account + "/" + peer).hashCode(), open,
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }
}
