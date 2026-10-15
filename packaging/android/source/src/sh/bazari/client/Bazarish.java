// Bazarish project (c) 2026
package sh.bazari.client;

import android.Manifest;
import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.provider.Settings;
import android.service.notification.StatusBarNotification;

public final class Bazarish {
    static final String CHANNEL_RUNNING = "running";
    static final String CHANNEL_MESSAGES = "messages";
    static final String CHANNEL_REACTIONS = "reactions";
    static final String CHANNEL_CALLS = "calls";
    static final String EXTRA_ACCOUNT = "sh.bazari.client.account";
    static final String EXTRA_PEER = "sh.bazari.client.peer";
    static final int RUNNING_ID = 1;
    private static final int CALL_ID = 2;
    private static final int MESSAGE_ID = 3;
    private static final int ASK_NOTIFICATIONS = 1;

    static String turnOff = "";
    static String turnOn = "";
    static String quit = "";
    static boolean notificationsOn = true;
    private static boolean started = false;

    static native void notificationsToggled();
    static native void quitRequested();
    static native void conversationOpened(String account, String peer);

    private Bazarish() {}

    public static void show(Context context, String running, String messages, String reactions,
            String calls, String turnOffText, String turnOnText, String quitText,
            boolean notifications) {
        turnOff = turnOffText;
        turnOn = turnOnText;
        quit = quitText;
        notificationsOn = notifications;
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        NotificationChannel runningChannel = new NotificationChannel(
            CHANNEL_RUNNING, running, NotificationManager.IMPORTANCE_LOW);
        runningChannel.setShowBadge(false);
        manager.createNotificationChannel(runningChannel);
        manager.createNotificationChannel(sounding(context, CHANNEL_MESSAGES, messages,
            NotificationManager.IMPORTANCE_HIGH, R.raw.notify));
        manager.createNotificationChannel(sounding(context, CHANNEL_REACTIONS, reactions,
            NotificationManager.IMPORTANCE_DEFAULT, R.raw.reaction));
        NotificationChannel callsChannel = new NotificationChannel(
            CHANNEL_CALLS, calls, NotificationManager.IMPORTANCE_HIGH);
        // The app rings itself, for as long as the call rings.
        callsChannel.setSound(null, null);
        manager.createNotificationChannel(callsChannel);
        if (started) {
            manager.notify(RUNNING_ID, BazarishService.running(context));
        } else {
            context.startForegroundService(new Intent(context, BazarishService.class));
            started = true;
        }
    }

    public static void notifyMessage(Context context, String account, String peer, String title,
            String body, boolean reaction) {
        Notification shown = new Notification.Builder(
                context, reaction ? CHANNEL_REACTIONS : CHANNEL_MESSAGES)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(title)
            .setContentText(body)
            .setStyle(new Notification.BigTextStyle().bigText(body))
            .setCategory(Notification.CATEGORY_MESSAGE)
            .setContentIntent(opening(context, account, peer))
            .setAutoCancel(true)
            .build();
        context.getSystemService(NotificationManager.class)
            .notify(account + "/" + peer, MESSAGE_ID, shown);
    }

    public static void notifyCall(Context context, String account, String peer, String title,
            String body) {
        Notification shown = new Notification.Builder(context, CHANNEL_CALLS)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(title)
            .setContentText(body)
            .setCategory(Notification.CATEGORY_CALL)
            .setContentIntent(opening(context, account, peer))
            .setOngoing(true)
            .build();
        context.getSystemService(NotificationManager.class).notify(CALL_ID, shown);
    }

    public static void cancelCall(Context context) {
        context.getSystemService(NotificationManager.class).cancel(CALL_ID);
    }

    public static void clearMessages(Context context) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        for (StatusBarNotification shown : manager.getActiveNotifications()) {
            if (shown.getId() == MESSAGE_ID) {
                manager.cancel(shown.getTag(), MESSAGE_ID);
            }
        }
    }

    public static boolean backgroundRestricted(Context context) {
        return !context.getSystemService(PowerManager.class)
            .isIgnoringBatteryOptimizations(context.getPackageName());
    }

    public static void openBackgroundSettings(Context context) {
        context.startActivity(new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
            Uri.parse("package:" + context.getPackageName())));
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

    public static void stop(Context context) {
        context.stopService(new Intent(context, BazarishService.class));
        context.getSystemService(NotificationManager.class).cancelAll();
        started = false;
    }

    private static NotificationChannel sounding(Context context, String id, String name,
            int importance, int sound) {
        NotificationChannel channel = new NotificationChannel(id, name, importance);
        channel.setSound(
            Uri.parse(ContentResolver.SCHEME_ANDROID_RESOURCE + "://"
                + context.getPackageName() + "/" + sound),
            new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_NOTIFICATION)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build());
        return channel;
    }

    private static PendingIntent opening(Context context, String account, String peer) {
        Intent open = new Intent(context, BazarishActivity.class)
            .putExtra(EXTRA_ACCOUNT, account)
            .putExtra(EXTRA_PEER, peer)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        return PendingIntent.getActivity(context, (account + "/" + peer).hashCode(), open,
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }
}
