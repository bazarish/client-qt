// Bazarish project (c) 2026
package sh.bazari.client;

import android.content.Intent;
import android.os.Bundle;

import java.lang.ref.WeakReference;
import java.lang.reflect.Field;

import org.qtproject.qt.android.QtActivityBase;
import org.qtproject.qt.android.bindings.QtActivity;

public class BazarishActivity extends QtActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        Bazarish.app = getApplicationContext();
        Bazarish.window = new WeakReference<>(this);
        Bazarish.holdConversation(this, getIntent());
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (Bazarish.ACTION_ANSWER.equals(intent.getAction())) {
            Bazarish.callAnswered();
            return;
        }
        String account = intent.getStringExtra(Bazarish.EXTRA_ACCOUNT);
        String peer = intent.getStringExtra(Bazarish.EXTRA_PEER);
        if (account != null && peer != null) {
            Bazarish.conversationOpened(account, peer);
        }
    }

    // Swiping the app out of recents destroys this activity, and Qt would end the process with it.
    @Override
    protected void onDestroy() {
        try {
            Field retain = QtActivityBase.class.getDeclaredField("m_retainNonConfigurationInstance");
            retain.setAccessible(true);
            retain.setBoolean(this, true);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("Qt's activity no longer lets the app outlive it", e);
        }
        super.onDestroy();
    }
}
