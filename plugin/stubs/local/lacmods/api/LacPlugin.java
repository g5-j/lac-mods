package local.lacmods.api;
/** Compile-time stub only; host supplies the real interface at runtime. */
public interface LacPlugin {
    void onStart(PluginContext context);
    void onStop();
}
