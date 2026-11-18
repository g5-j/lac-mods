package lacadmin;
import local.lacmods.api.LacPlugin;
import local.lacmods.api.PluginContext;
/** LAC Online 1.9.7 server-side inventory grant command. */
public final class AdminGivePlugin implements LacPlugin {
    @Override public void onStart(PluginContext context) {
        context.log("AdminGive: starting for LAC 1.9.7");
        System.loadLibrary("lac_admin_give");
        nativeStart();
    }
    @Override public void onStop() { nativeStop(); }
    private static native void nativeStart();
    private static native void nativeStop();
}
