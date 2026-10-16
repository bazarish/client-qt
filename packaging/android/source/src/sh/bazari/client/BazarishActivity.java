// Bazarish project (c) 2026
package sh.bazari.client;

import android.content.Intent;
import android.os.Bundle;

import java.lang.ref.WeakReference;

import org.qtproject.qt.android.bindings.QtActivity;

// The window: its process may end with it, the core goes on in its own.
public class BazarishActivity extends QtActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        Window.app = getApplicationContext();
        Window.window = new WeakReference<>(this);
        Window.hold(this, getIntent());
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
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
}
