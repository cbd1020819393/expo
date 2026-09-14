/**
 * Type declarations for the NAPI exports of `libexpo_gl.so` (ExpoGLNapi.cpp).
 *
 * These exports are a bypass channel used by `ExpoGLView.ets`: the TurboModule
 * channel cannot reach the UIAbilityContext (cache dir) and has no thread-safe
 * Fabric event delivery for the surface-ready notification, so the ArkTS view
 * imports the .so directly.
 *
 * `exgl_<id>` is the XComponent id prefix agreed between ExpoGLView.ets
 * (XCOMPONENT_ID_PREFIX) and the native side.
 */
declare interface ExpoGlNativeModule {
  /**
   * Register the sandbox cache directory used by takeSnapshotAsync to write
   * GLView/*.png|jpeg files. Pass `uiAbilityContext.cacheDir`.
   */
  setCacheDir(dir: string): void;

  /**
   * Register the single surface-ready listener. Invoked from the native GL
   * host once a surface's GL context has been prepared; routes by XComponent
   * id. Only the first registration is honored (subsequent calls are no-ops).
   */
  registerSurfaceListener(listener: (xcomponentId: string, exglCtxId: number) => void): void;
}

declare module 'libexpo_gl.so' {
  const exglNative: ExpoGlNativeModule;
  export default exglNative;
}
