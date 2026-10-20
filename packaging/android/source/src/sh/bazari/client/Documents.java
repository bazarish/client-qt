// Bazarish project (c) 2026
package sh.bazari.client;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.OutputStream;

// A document the user picked is a content:// address that only its provider can open.
public final class Documents {
    private static final String TAG = "bazarish";

    private Documents() {}

    // Into the shared Downloads, which renames on a clash itself; false when it refuses the file.
    public static boolean saveToDownloads(Context context, String name, String mime, byte[] bytes) {
        ContentResolver resolver = context.getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
        values.put(MediaStore.MediaColumns.MIME_TYPE, mime);
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri saved = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (saved == null) {
            Log.w(TAG, "Downloads did not take a file");
            return false;
        }
        try (OutputStream out = resolver.openOutputStream(saved)) {
            out.write(bytes);
        } catch (Exception e) {
            Log.w(TAG, "a file for Downloads was not written: " + e);
            resolver.delete(saved, null, null);
            return false;
        }
        values.clear();
        values.put(MediaStore.MediaColumns.IS_PENDING, 0);
        resolver.update(saved, values, null, null);
        return true;
    }

    // A descriptor the caller owns and closes, or -1 when the provider refuses it.
    public static int open(Context context, String uri, String mode) {
        try {
            ParcelFileDescriptor document =
                context.getContentResolver().openFileDescriptor(Uri.parse(uri), mode);
            return document == null ? -1 : document.detachFd();
        } catch (Exception e) {
            Log.w(TAG, "a document did not open (" + mode + "): " + e);
            return -1;
        }
    }

    // The name the provider shows, or "" when it no longer answers for the document.
    public static String name(Context context, String uri) {
        String[] column = {OpenableColumns.DISPLAY_NAME};
        try (Cursor row =
                 context.getContentResolver().query(Uri.parse(uri), column, null, null, null)) {
            return row != null && row.moveToFirst() && !row.isNull(0) ? row.getString(0) : "";
        } catch (Exception e) {
            Log.w(TAG, "a document did not answer for its name: " + e);
            return "";
        }
    }

    // Bytes, or -1 when the provider does not say.
    public static long size(Context context, String uri) {
        String[] column = {OpenableColumns.SIZE};
        try (Cursor row =
                 context.getContentResolver().query(Uri.parse(uri), column, null, null, null)) {
            return row != null && row.moveToFirst() && !row.isNull(0) ? row.getLong(0) : -1;
        } catch (Exception e) {
            Log.w(TAG, "a document did not answer for its size: " + e);
            return -1;
        }
    }

    public static boolean delete(Context context, String uri) {
        try {
            return DocumentsContract.deleteDocument(context.getContentResolver(), Uri.parse(uri));
        } catch (Exception e) {
            Log.w(TAG, "a document was not deleted: " + e);
            return false;
        }
    }
}
