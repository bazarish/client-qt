// Bazarish project (c) 2026
package sh.bazari.client;

import android.content.Intent;

import org.qtproject.qt.android.bindings.QtActivity;

public class BazarishActivity extends QtActivity {
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        String account = intent.getStringExtra(Bazarish.EXTRA_ACCOUNT);
        String peer = intent.getStringExtra(Bazarish.EXTRA_PEER);
        if (account != null && peer != null) {
            Bazarish.conversationOpened(account, peer);
        }
    }

    // Qt ends the process with its activity, so the service and every notice go first.
    @Override
    protected void onDestroy() {
        if (!isChangingConfigurations()) {
            Bazarish.stop(this);
        }
        super.onDestroy();
    }
}
