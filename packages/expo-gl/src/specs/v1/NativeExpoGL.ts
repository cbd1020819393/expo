/**
 * TurboModule Spec for the ExpoGL native module (HarmonyOS / RNOH).
 *
 * Mirrors the public surface of the original expo-modules-core based
 * `ExpoGL` module (see android/src/main/java/expo/modules/gl/GLModule.kt and
 * ios/ExpoGLModule.swift in the upstream package):
 *
 *  - createContextAsync(): resolves `{ exglCtxId }` for a headless context
 *  - destroyContextAsync(exglCtxId): resolves `true` if the context existed
 *  - takeSnapshotAsync(exglCtxId, options): resolves a GLSnapshot-like object
 *  - destroyObjectAsync(exglObjId): resolves `true` if the object existed
 *
 * `prepareGLRuntime` is an OHOS-specific addition: the WebGL bindings
 * (constructors, constants and prototype methods installed by the C++ core
 * onto `global.__EXGLContexts`) need a `jsi::Runtime` pointer which is only
 * reachable from inside a native TurboModule call. The JS wrapper invokes it
 * once, eagerly, at import time — mirroring the original `OnCreate` hook
 * (`EXGLInstallWebGLBindings`) on Android/iOS.
 */
import type { TurboModule } from 'react-native/Libraries/TurboModule/RCTExport';
import { TurboModuleRegistry } from 'react-native';

export interface SnapshotRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface SnapshotFramebuffer {
  id: number;
}

export interface SnapshotOptions {
  flip?: boolean;
  framebuffer?: SnapshotFramebuffer;
  rect?: SnapshotRect;
  format?: string;
  compress?: number;
}

export interface ContextCreateResult {
  exglCtxId: number;
}

export interface SnapshotResult {
  uri: string;
  localUri: string;
  width: number;
  height: number;
}

export interface Spec extends TurboModule {
  /**
   * Idempotent. Caches the current `jsi::Runtime` pointer and installs the
   * WebGL global constructors/constants so `global.__EXGLContexts` can be
   * populated later by surface/headless context creation.
   */
  prepareGLRuntime(): void;

  createContextAsync(): Promise<ContextCreateResult>;

  destroyContextAsync(exglCtxId: number): Promise<boolean>;

  takeSnapshotAsync(exglCtxId: number, options: SnapshotOptions): Promise<SnapshotResult>;

  destroyObjectAsync(exglObjId: number): Promise<boolean>;
}

export default TurboModuleRegistry.get<Spec>('ExpoGL') as Spec | null;
