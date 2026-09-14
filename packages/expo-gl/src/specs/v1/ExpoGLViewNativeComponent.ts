/**
 * Fabric component Spec for the ExpoGL native view (HarmonyOS / RNOH).
 *
 * Mirrors the original expo-modules view: the native view owns an OpenGL ES
 * surface (XComponent on HarmonyOS instead of TextureView/CALayer) and fires
 * `onSurfaceCreate` with `{ exglCtxId }` once the GL context has been created
 * and installed onto `global.__EXGLContexts`.
 *
 * `msaaSamples` is iOS-only in the upstream library (multisampling via
 * APPLE_framebuffer_multisample); it is kept in the Spec for API parity and
 * ignored by the HarmonyOS implementation, matching the Android behavior
 * where GLView.kt never declares the prop.
 */
import type { ViewProps } from 'react-native/Libraries/Components/View/ViewPropTypes';
import type { HostComponent } from 'react-native';
import codegenNativeComponent from 'react-native/Libraries/Utilities/codegenNativeComponent';
import type { Int32, DirectEventHandler } from 'react-native/Libraries/Types/CodegenTypes';

export interface SurfaceCreateEvent {
  exglCtxId: Int32;
}

export interface NativeProps extends ViewProps {
  msaaSamples?: Int32;
  enableExperimentalWorkletSupport?: boolean;
  onSurfaceCreate?: DirectEventHandler<SurfaceCreateEvent>;
}

export default codegenNativeComponent<NativeProps>(
  'ExpoGLView'
) as HostComponent<NativeProps>;
