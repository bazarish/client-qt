// Bazarish project (c) 2026
package sh.bazari.client;

import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.util.Log;

// A document the user picked is a content:// address that only its provider can open.
public final class Documents {
    private static final String TAG = "bazarish";

    private Documents() {}

    // A descriptor the caller owns and closes, or -1 when the provider refuses it.
    public static int openForWriting(Context context, String uri) {
        try {
            ParcelFileDescriptor document =
                context.getContentResolver().openFileDescriptor(Uri.parse(uri), "wt");
            return document == null ? -1 : document.detachFd();
        } catch (Exception e) {
            Log.w(TAG, "a document did not open for writing: " + e);
            return -1;
        }
    }
}
