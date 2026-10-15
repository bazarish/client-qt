// Bazarish project (c) 2026
package sh.bazari.client;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.os.IBinder;
import android.os.PowerManager;

public class BazarishService extends Service {
    private static final String ACTION_TOGGLE = "sh.bazari.client.TOGGLE_NOTIFICATIONS";
    private static final String ACTION_QUIT = "sh.bazari.client.QUIT";
    private static final int OPEN_REQUEST = 0;
    private static final int TOGGLE_REQUEST = 1;
    private static final int QUIT_REQUEST = 2;

    private PowerManager.WakeLock awake;

    @Override
    public void onCreate() {
        super.onCreate();
        // Tunnels expire in minutes; a sleeping CPU lets them lapse and no mail can arrive.
        awake = getSystemService(PowerManager.class)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "bazarish:running");
        awake.acquire();
    }

    @Override
    public void onDestroy() {
        awake.release();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();
        if (ACTION_TOGGLE.equals(action)) {
            Bazarish.notificationsToggled();
        } else if (ACTION_QUIT.equals(action)) {
            Bazarish.quitRequested();
        }
        startForeground(Bazarish.RUNNING_ID, running(this),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_REMOTE_MESSAGING);
        return START_NOT_STICKY;
    }

    static Notification running(Context context) {
        Intent launch = context.getPackageManager()
            .getLaunchIntentForPackage(context.getPackageName());
        PendingIntent open = PendingIntent.getActivity(context, OPEN_REQUEST, launch,
            PendingIntent.FLAG_IMMUTABLE);
        Icon icon = Icon.createWithResource(context, R.drawable.ic_notification);
        return new Notification.Builder(context, Bazarish.CHANNEL_RUNNING)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentIntent(open)
            .setOngoing(true)
            .setShowWhen(false)
            .addAction(new Notification.Action.Builder(icon,
                Bazarish.notificationsOn ? Bazarish.turnOff : Bazarish.turnOn,
                acting(context, ACTION_TOGGLE, TOGGLE_REQUEST)).build())
            .addAction(new Notification.Action.Builder(icon, Bazarish.quit,
                acting(context, ACTION_QUIT, QUIT_REQUEST)).build())
            .build();
    }

    private static PendingIntent acting(Context context, String action, int request) {
        return PendingIntent.getService(context, request,
            new Intent(context, BazarishService.class).setAction(action),
            PendingIntent.FLAG_IMMUTABLE);
    }
}
