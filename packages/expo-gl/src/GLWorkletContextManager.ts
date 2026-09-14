import type { ExpoWebGLRenderingContext } from './GLView.types';

/**
 * HarmonyOS first-phase behavior (per the adaptation plan): the experimental
 * Reanimated worklet support is disabled. The upstream implementation does a
 * soft `require('react-native-reanimated')` and falls back to the same
 * "Worklet runtime is not available" error when reanimated cannot provide a
 * worklet runtime, so the observable behavior here (throwing on
 * `getWorkletContext`) matches the upstream when no worklet runtime exists.
 *
 * The native side (`EXGLContextPrepareWorklet` in common/) keeps the full
 * worklet path compiled in, so enabling this later only requires restoring the
 * soft-require of an OHOS-adapted reanimated here.
 */
export function createWorkletContextManager(): {
  getContext: (contextId: number) => ExpoWebGLRenderingContext | undefined;
  unregister?: (contextId: number) => void;
} {
  return {
    getContext: () => {
      throw new Error('Worklet runtime is not available');
    },
  };
}
