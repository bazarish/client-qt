// Bazarish project (c) 2026
package sh.bazari.client;

import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

import java.lang.ref.WeakReference;

import org.qtproject.qt.android.bindings.QtActivity;

// The window: its process may end with it, the core goes on in its own.
public class BazarishActivity extends QtActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        Window.app = getApplicationContext();
        Window.window = new WeakReference<>(this);
        if (fromNotice(getIntent())) {
            Window.hold(this, getIntent());
            showForRinging(getIntent());
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (!fromNotice(intent)) {
            return;
        }
        showForRinging(intent);
        if (Shade.ACTION_ANSWER.equals(intent.getAction())) {
            Window.callAnswered();
            return;
        }
        String account = intent.getStringExtra(Shade.EXTRA_ACCOUNT);
        String peer = intent.getStringExtra(Shade.EXTRA_PEER);
        if (account != null && peer != null) {
            Window.conversationOpened(account, peer);
        }
    }

    private static boolean fromNotice(Intent intent) {
        return intent != null && intent.getComponent() != null
            && Shade.NOTICE_ENTRY.equals(intent.getComponent().getClassName());
    }

    // A ringing call is shown over the lock at once; the window learns of the call seconds later.
    private void showForRinging(Intent intent) {
        if (intent != null && Shade.ACTION_RING.equals(intent.getAction())) {
            setShowWhenLocked(true);
            setTurnScreenOn(true);
        }
    }

    // Qt cannot start twice in one process, and its teardown hangs; the core keeps the rest.
    @Override
    protected void onDestroy() {
        Process.killProcess(Process.myPid());
    }
}
