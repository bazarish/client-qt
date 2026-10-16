// Bazarish project (c) 2026
package sh.bazari.client;

import android.app.Notification;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.os.PowerManager;

import org.qtproject.qt.android.QtServiceBase;

// Started at boot it runs Qt itself, with no window; next to the activity it only holds the process.
public class BazarishService extends QtServiceBase {
    private static final String ACTION_TOGGLE = "sh.bazari.client.TOGGLE_NOTIFICATIONS";
    private static final String ACTION_QUIT = "sh.bazari.client.QUIT";
    private static final String ACTION_DECLINE = "sh.bazari.client.DECLINE";
    private static final String ACTION_SHOW = "sh.bazari.client.SHOW";
    private static final int OPEN_REQUEST = 0;
    private static final int TOGGLE_REQUEST = 1;
    private static final int QUIT_REQUEST = 2;
    private static final int DECLINE_REQUEST = 3;
    private static final int SHOW_REQUEST = 4;

    private PowerManager.WakeLock awake;

    @Override
    public void onCreate() {
        Bazarish.app = getApplicationContext();
        // Tunnels expire in minutes; a sleeping CPU lets them lapse and no mail can arrive.
        awake = getSystemService(PowerManager.class)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "bazarish:running");
        awake.acquire();
        super.onCreate();
    }

    @Override
    public void onDestroy() {
        awake.release();
        super.onDestroy();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();
        if (ACTION_TOGGLE.equals(action)) {
            Bazarish.notificationsToggled();
        } else if (ACTION_QUIT.equals(action)) {
            Bazarish.quitRequested();
        } else if (ACTION_DECLINE.equals(action)) {
            Bazarish.callDeclined();
        }
        startForeground(Bazarish.PRESENCE_ID, presence(this),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_REMOTE_MESSAGING);
        return START_NOT_STICKY;
    }

    static Notification presence(Context context) {
        Intent launch = context.getPackageManager()
            .getLaunchIntentForPackage(context.getPackageName());
        PendingIntent open = PendingIntent.getActivity(context, OPEN_REQUEST, launch,
            PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder shown = new Notification.Builder(context, Bazarish.CHANNEL_PRESENCE)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentIntent(open)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setShowWhen(false)
            // Android 14 lets the user swipe even this away; it is put back at once.
            .setDeleteIntent(acting(context, ACTION_SHOW, SHOW_REQUEST));
        if (!Bazarish.unread.isEmpty()) {
            shown.setContentText(Bazarish.unread);
        }
        // Until Qt has given its labels there is nothing to name the buttons with.
        if (!Bazarish.quit.isEmpty()) {
            Icon icon = Icon.createWithResource(context, R.drawable.ic_notification);
            shown.addAction(new Notification.Action.Builder(icon,
                Bazarish.notificationsOn ? Bazarish.turnOff : Bazarish.turnOn,
                acting(context, ACTION_TOGGLE, TOGGLE_REQUEST)).build());
            shown.addAction(new Notification.Action.Builder(icon, Bazarish.quit,
                acting(context, ACTION_QUIT, QUIT_REQUEST)).build());
        }
        return shown.build();
    }

    static PendingIntent declining(Context context) {
        return acting(context, ACTION_DECLINE, DECLINE_REQUEST);
    }

    private static PendingIntent acting(Context context, String action, int request) {
        return PendingIntent.getService(context, request,
            new Intent(context, BazarishService.class).setAction(action),
            PendingIntent.FLAG_IMMUTABLE);
    }
}
