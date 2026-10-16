// Bazarish project (c) 2026
package sh.bazari.client;

import android.Manifest;
import android.app.Activity;
import android.app.KeyguardManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.app.Person;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.provider.Settings;
import android.service.notification.StatusBarNotification;

import java.io.IOException;
import java.lang.ref.WeakReference;

public final class Bazarish {
    static final String CHANNEL_PRESENCE = "status";
    static final String CHANNEL_MESSAGES = "message";
    static final String CHANNEL_REACTIONS = "reaction";
    static final String CHANNEL_CALLS = "calls";
    static final String EXTRA_ACCOUNT = "sh.bazari.client.account";
    static final String EXTRA_PEER = "sh.bazari.client.peer";
    static final String ACTION_ANSWER = "sh.bazari.client.ANSWER";
    static final int PRESENCE_ID = 1;
    private static final int CALL_ID = 2;
    private static final int MESSAGE_ID = 3;
    private static final int ASK_NOTIFICATIONS = 1;
    private static final int ANSWER_REQUEST = 1;
    private static final long WAKE_MILLIS = 3000;
    private static final String HELD = "held";
    private static MediaPlayer ring = null;

    static Context app;
    static WeakReference<Activity> window = new WeakReference<>(null);
    static String turnOff = "";
    static String turnOn = "";
    static String quit = "";
    static String line = "";
    static boolean notificationsOn = true;
    private static boolean started = false;

    static native void notificationsToggled();
    static native void quitRequested();
    static native void conversationOpened(String account, String peer);
    static native void callAnswered();
    static native void callDeclined();
    static native void unlocked();

    private Bazarish() {}

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
        if (started) {
            manager.notify(PRESENCE_ID, BazarishService.presence(app));
        } else {
            app.startForegroundService(new Intent(app, BazarishService.class));
            started = true;
        }
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

    // Only while a call rings or runs: the app is not to be read over the lock screen otherwise.
    public static void overLockScreen(boolean on) {
        Activity shown = window.get();
        if (shown == null) {
            return;
        }
        shown.runOnUiThread(() -> {
            shown.setShowWhenLocked(on);
            shown.setTurnScreenOn(on);
        });
    }

    // The rest of the app is behind the lock: leaving the call screen asks for the unlock first.
    public static void dismissKeyguard() {
        Activity shown = window.get();
        if (shown == null) {
            unlocked();
            return;
        }
        shown.runOnUiThread(() -> {
            KeyguardManager keyguard = shown.getSystemService(KeyguardManager.class);
            if (!keyguard.isKeyguardLocked()) {
                unlocked();
                return;
            }
            keyguard.requestDismissKeyguard(shown, new KeyguardManager.KeyguardDismissCallback() {
                @Override
                public void onDismissSucceeded() {
                    unlocked();
                }
            });
        });
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

    public static void clearMessages() {
        NotificationManager manager = app.getSystemService(NotificationManager.class);
        for (StatusBarNotification shown : manager.getActiveNotifications()) {
            if (shown.getId() == MESSAGE_ID) {
                manager.cancel(shown.getTag(), MESSAGE_ID);
            }
        }
    }

    public static boolean backgroundRestricted() {
        return !app.getSystemService(PowerManager.class)
            .isIgnoringBatteryOptimizations(app.getPackageName());
    }

    public static boolean fullScreenCallsRestricted() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE
            && !app.getSystemService(NotificationManager.class).canUseFullScreenIntent();
    }

    public static void openFullScreenCallsSettings() {
        app.startActivity(new Intent(Settings.ACTION_MANAGE_APP_USE_FULL_SCREEN_INTENT,
                Uri.parse("package:" + app.getPackageName()))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
    }

    public static void openBackgroundSettings() {
        app.startActivity(new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                Uri.parse("package:" + app.getPackageName()))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
    }

    public static void askNotifications(Context context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
            && context instanceof Activity
            && context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            ((Activity) context).requestPermissions(
                new String[] {Manifest.permission.POST_NOTIFICATIONS}, ASK_NOTIFICATIONS);
        }
    }

    public static void stop() {
        app.getSystemService(NotificationManager.class).cancelAll();
        app.stopService(new Intent(app, BazarishService.class));
    }

    // Written before Qt starts: a Qt that is already running restarts the process.
    static void holdConversation(Context context, Intent intent) {
        String account = intent == null ? null : intent.getStringExtra(EXTRA_ACCOUNT);
        String peer = intent == null ? null : intent.getStringExtra(EXTRA_PEER);
        if (account == null || peer == null) {
            return;
        }
        if (!context.getSharedPreferences(HELD, Context.MODE_PRIVATE).edit()
                .putString(EXTRA_ACCOUNT, account)
                .putString(EXTRA_PEER, peer)
                .commit()) {
            throw new IllegalStateException("the chat a notice was tapped for was not kept");
        }
    }

    public static String[] takeHeldConversation() {
        SharedPreferences held = app.getSharedPreferences(HELD, Context.MODE_PRIVATE);
        String account = held.getString(EXTRA_ACCOUNT, null);
        String peer = held.getString(EXTRA_PEER, null);
        if (!held.edit().clear().commit()) {
            throw new IllegalStateException("the chat a notice was tapped for was not cleared");
        }
        if (account == null || peer == null) {
            return new String[0];
        }
        return new String[] {account, peer};
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
