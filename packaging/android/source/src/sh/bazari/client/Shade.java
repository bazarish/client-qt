// Bazarish project (c) 2026
package sh.bazari.client;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Person;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.PowerManager;
import android.os.ResultReceiver;
import android.service.notification.StatusBarNotification;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

// The core's side of Android: the shade, the notices and the ring, all in the :core process.
public final class Shade {
    static final String CHANNEL_PRESENCE = "status";
    static final String CHANNEL_MESSAGES = "message";
    static final String CHANNEL_REACTIONS = "reaction";
    static final String CHANNEL_CALLS = "calls";
    static final String EXTRA_ACCOUNT = "sh.bazari.client.account";
    static final String EXTRA_PEER = "sh.bazari.client.peer";
    static final String EXTRA_LISTENER = "sh.bazari.client.listener";
    static final String ACTION_ANSWER = "sh.bazari.client.ANSWER";
    static final String ACTION_TOGGLE = "sh.bazari.client.TOGGLE_NOTIFICATIONS";
    static final String ACTION_QUIT = "sh.bazari.client.QUIT";
    static final String ACTION_DECLINE = "sh.bazari.client.DECLINE";
    static final int PRESENCE_ID = 1;
    private static final int CALL_ID = 2;
    private static final int MESSAGE_ID = 3;
    private static final int ANSWER_REQUEST = 1;
    private static final long WAKE_MILLIS = 3000;
    private static MediaPlayer ring = null;

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
        manager.createNotificationChannel(sounding(CHANNEL_MESSAGES, messages,
            NotificationManager.IMPORTANCE_HIGH, "notify"));
        manager.createNotificationChannel(sounding(CHANNEL_REACTIONS, reactions,
            NotificationManager.IMPORTANCE_DEFAULT, "reaction"));
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
            new Intent(app, BazarishActivity.class).setAction(ACTION_ANSWER)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent opened = opening(account, peer);
        Notification shown = new Notification.Builder(app, CHANNEL_CALLS)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentText(body)
            .setStyle(Notification.CallStyle.forIncomingCall(
                caller, BazarishService.declining(app), answer))
            .setCategory(Notification.CATEGORY_CALL)
            .setContentIntent(opened)
            .setFullScreenIntent(opened, true)
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
    }

    public static void stopRinging() {
        if (ring == null) {
            return;
        }
        ring.stop();
        ring.release();
        ring = null;
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

    private static PendingIntent opening(String account, String peer) {
        Intent open = new Intent(app, BazarishActivity.class)
            .putExtra(EXTRA_ACCOUNT, account)
            .putExtra(EXTRA_PEER, peer)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        return PendingIntent.getActivity(app, (account + "/" + peer).hashCode(), open,
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }
}
