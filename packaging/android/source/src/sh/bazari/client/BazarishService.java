// Bazarish project (c) 2026
package sh.bazari.client;

import android.app.Notification;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.os.PowerManager;
import android.os.ResultReceiver;

import java.lang.ref.WeakReference;

import org.qtproject.qt.android.QtServiceBase;

// The core: its own process, started by the window or at boot, and the only way out is Quit.
public class BazarishService extends QtServiceBase {
    private static final String ACTION_SHOW = "sh.bazari.client.SHOW";
    private static final int OPEN_REQUEST = 0;
    private static final int TOGGLE_REQUEST = 1;
    private static final int QUIT_REQUEST = 2;
    private static final int DECLINE_REQUEST = 3;
    private static final int SHOW_REQUEST = 4;

    static volatile boolean talking = false;
    static WeakReference<BazarishService> running = new WeakReference<>(null);

    private PowerManager.WakeLock awake;

    @Override
    public void onCreate() {
        Shade.app = getApplicationContext();
        // Tunnels expire in minutes; a sleeping CPU lets them lapse and no mail can arrive.
        awake = getSystemService(PowerManager.class)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "bazarish:running");
        awake.acquire();
        // Android gives a started service seconds to come to the foreground; loading Qt takes longer.
        Shade.presenceChannel(this, getApplicationInfo().loadLabel(getPackageManager()).toString());
        running = new WeakReference<>(this);
        comeForward();
        super.onCreate();
    }

    @Override
    public void onDestroy() {
        awake.release();
        super.onDestroy();
    }

    @Override
    @SuppressWarnings("deprecation")
    public int onStartCommand(Intent intent, int flags, int startId) {
        comeForward();
        if (intent == null) {
            return START_NOT_STICKY;
        }
        ResultReceiver listener = intent.getParcelableExtra(Shade.EXTRA_LISTENER);
        if (listener != null) {
            Shade.whenListening(listener);
        }
        if (intent.getAction() != null) {
            Shade.act(intent.getAction());
        }
        return START_NOT_STICKY;
    }

    void comeForward() {
        int types = ServiceInfo.FOREGROUND_SERVICE_TYPE_REMOTE_MESSAGING;
        if (talking) {
            types |= ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE;
        }
        startForeground(Shade.PRESENCE_ID, presence(this), types);
    }

    static Notification presence(Context context) {
        Intent launch = context.getPackageManager()
            .getLaunchIntentForPackage(context.getPackageName());
        PendingIntent open = PendingIntent.getActivity(context, OPEN_REQUEST, launch,
            PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder shown = new Notification.Builder(context, Shade.CHANNEL_PRESENCE)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentIntent(open)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setShowWhen(false)
            // Android 14 lets the user swipe even this away; it is put back at once.
            .setDeleteIntent(acting(context, ACTION_SHOW, SHOW_REQUEST));
        // A notice with no title still keeps room for one, which leaves a blank band.
        if (!Shade.line.isEmpty()) {
            shown.setContentTitle(Shade.line);
        }
        // Until Qt has given its labels there is nothing to name the buttons with.
        if (!Shade.quit.isEmpty()) {
            Icon icon = Icon.createWithResource(context, R.drawable.ic_notification);
            shown.addAction(new Notification.Action.Builder(icon,
                Shade.notificationsOn ? Shade.turnOff : Shade.turnOn,
                acting(context, Shade.ACTION_TOGGLE, TOGGLE_REQUEST)).build());
            shown.addAction(new Notification.Action.Builder(icon, Shade.quit,
                acting(context, Shade.ACTION_QUIT, QUIT_REQUEST)).build());
        }
        return shown.build();
    }

    static PendingIntent declining(Context context) {
        return acting(context, Shade.ACTION_DECLINE, DECLINE_REQUEST);
    }

    private static PendingIntent acting(Context context, String action, int request) {
        return PendingIntent.getService(context, request,
            new Intent(context, BazarishService.class).setAction(action),
            PendingIntent.FLAG_IMMUTABLE);
    }
}
