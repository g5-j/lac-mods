package lacadmin;

import java.io.PrintWriter;
import java.io.StringWriter;

import local.lacmods.api.LacPlugin;
import local.lacmods.api.PluginContext;

/** LAC Online 1.9.7 server-side inventory grant command. */
public final class AdminGivePlugin implements LacPlugin {
    private boolean libraryLoaded;
    private boolean startRequested;

    @Override
    public synchronized void onStart(PluginContext context) {
        if (startRequested) {
            context.log("AdminGive: start already requested");
            return;
        }
        try {
            context.log("AdminGive: loading native library for LAC 1.9.7");
            System.loadLibrary("lac_admin_give");
            libraryLoaded = true;
            context.log("AdminGive: native library loaded; JNI methods registered");
            nativeStart();
            startRequested = true;
            context.log("AdminGive: native hook installation queued; check native log for final status");
        } catch (Throwable error) {
            context.log("AdminGive: START FAILED\n" + stackTrace(error));
            if (libraryLoaded) {
                try {
                    nativeStop();
                } catch (Throwable stopError) {
                    context.log("AdminGive: cleanup after start failure also failed: " + stopError);
                }
            }
            throw new IllegalStateException("AdminGive failed to initialize", error);
        }
    }

    @Override
    public synchronized void onStop() {
        if (!libraryLoaded || !startRequested) return;
        try {
            nativeStop();
        } catch (Throwable error) {
            // Keep shutdown best-effort: do not mask the original plugin lifecycle error.
        } finally {
            startRequested = false;
        }
    }

    private static String stackTrace(Throwable error) {
        StringWriter writer = new StringWriter();
        error.printStackTrace(new PrintWriter(writer));
        return writer.toString();
    }

    private static native void nativeStart();
    private static native void nativeStop();
}
