// Bazarish project (c) 2026
package sh.bazari.client;

import android.Manifest;
import android.app.Activity;
import android.app.KeyguardManager;
import android.app.NotificationManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.ResultReceiver;
import android.provider.Settings;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import java.lang.ref.WeakReference;

// The window's side of Android: what only an activity can do, in the window's own process.
public final class Window {
    private static final int ASK_NOTIFICATIONS = 1;
    private static final String HELD = "held";
    private static final String HELD_ANSWER = "answer";
    private static final String HELD_LINK = "link";

    private static final int LIGHT_BARS = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
        | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;

    static Context app;
    static WeakReference<Activity> window = new WeakReference<>(null);
    private static Integer systemStatusBar;
    private static int systemNavigationBar;
    private static int systemBarsLook;

    static native void conversationOpened(String account, String peer);
    static native void callAnswered();
    static native void linkOpened(String link);
    static native void unlocked();
    static native void coreListening();
    static native void keyboardCovers(int pixels);

    private Window() {}

    // The core may be up already or not at all: either way it answers once it listens.
    public static void startCore() {
        ResultReceiver listener = new ResultReceiver(new Handler(Looper.getMainLooper())) {
            @Override
            protected void onReceiveResult(int resultCode, Bundle resultData) {
                coreListening();
            }
        };
        app.startForegroundService(new Intent(app, BazarishService.class)
            .putExtra(Shade.EXTRA_LISTENER, listener));
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

    // The window keeps its size under the keyboard, so the content is told how much it covers.
    public static void followKeyboard() {
        Activity shown = window.get();
        if (shown == null) {
            return;
        }
        shown.runOnUiThread(() -> {
            View frame = shown.getWindow().getDecorView();
            frame.setOnApplyWindowInsetsListener((view, insets) -> {
                int keyboard = insets.getInsets(WindowInsets.Type.ime()).bottom;
                int bars = insets.getInsets(WindowInsets.Type.navigationBars()).bottom;
                keyboardCovers(Math.max(0, keyboard - bars));
                return view.onApplyWindowInsets(insets);
            });
            frame.requestApplyInsets();
        });
    }

    // The bars as the system drew them come back with the dark theme, so it looks as it always did.
    public static void paintSystemBars(boolean light, int color) {
        Activity shown = window.get();
        if (shown == null || (!light && systemStatusBar == null)) {
            return;
        }
        shown.runOnUiThread(() -> {
            android.view.Window frame = shown.getWindow();
            WindowInsetsController bars = frame.getInsetsController();
            if (systemStatusBar == null) {
                systemStatusBar = frame.getStatusBarColor();
                systemNavigationBar = frame.getNavigationBarColor();
                systemBarsLook = bars.getSystemBarsAppearance();
            }
            frame.setStatusBarColor(light ? color : systemStatusBar);
            frame.setNavigationBarColor(light ? color : systemNavigationBar);
            bars.setSystemBarsAppearance(light ? LIGHT_BARS : systemBarsLook & LIGHT_BARS, LIGHT_BARS);
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

    public static void clearMessages() {
        Shade.clearMessages(app);
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

    public static void askNotifications() {
        Activity shown = window.get();
        if (shown != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
            && shown.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            shown.requestPermissions(
                new String[] {Manifest.permission.POST_NOTIFICATIONS}, ASK_NOTIFICATIONS);
        }
    }

    // Written before Qt starts: what a notice was tapped for waits for the window to come up.
    static void hold(Context context, Intent intent) {
        if (intent == null) {
            return;
        }
        String account = intent.getStringExtra(Shade.EXTRA_ACCOUNT);
        String peer = intent.getStringExtra(Shade.EXTRA_PEER);
        boolean answer = Shade.ACTION_ANSWER.equals(intent.getAction());
        if (!answer && (account == null || peer == null)) {
            return;
        }
        if (!context.getSharedPreferences(HELD, Context.MODE_PRIVATE).edit()
                .putString(Shade.EXTRA_ACCOUNT, account)
                .putString(Shade.EXTRA_PEER, peer)
                .putBoolean(HELD_ANSWER, answer)
                .commit()) {
            throw new IllegalStateException("what a notice was tapped for was not kept");
        }
    }

    // A bazarish:// link from any app: it only opens the window it names.
    static void holdLink(Context context, String link) {
        if (!context.getSharedPreferences(HELD, Context.MODE_PRIVATE).edit()
                .putString(HELD_LINK, link)
                .commit()) {
            throw new IllegalStateException("the link the app was opened with was not kept");
        }
    }

    public static String takeHeldLink() {
        SharedPreferences held = app.getSharedPreferences(HELD, Context.MODE_PRIVATE);
        String link = held.getString(HELD_LINK, "");
        if (!held.edit().remove(HELD_LINK).commit()) {
            throw new IllegalStateException("the link the app was opened with was not cleared");
        }
        return link;
    }

    public static String[] takeHeldConversation() {
        SharedPreferences held = app.getSharedPreferences(HELD, Context.MODE_PRIVATE);
        String account = held.getString(Shade.EXTRA_ACCOUNT, null);
        String peer = held.getString(Shade.EXTRA_PEER, null);
        if (!held.edit().remove(Shade.EXTRA_ACCOUNT).remove(Shade.EXTRA_PEER).commit()) {
            throw new IllegalStateException("the chat a notice was tapped for was not cleared");
        }
        if (account == null || peer == null) {
            return new String[0];
        }
        return new String[] {account, peer};
    }

    public static boolean takeHeldAnswer() {
        SharedPreferences held = app.getSharedPreferences(HELD, Context.MODE_PRIVATE);
        boolean answer = held.getBoolean(HELD_ANSWER, false);
        if (!held.edit().remove(HELD_ANSWER).commit()) {
            throw new IllegalStateException("the call a notice was answered for was not cleared");
        }
        return answer;
    }
}
