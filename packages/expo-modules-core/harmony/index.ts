/**
 * HarmonyOS (RNOH) entry for @react-native-ohos/expo-modules-core.
 *
 * This package only ever resolves on the harmony platform (via the
 * `harmony.alias` redirect in RNOH's metro resolver). Android/iOS keep
 * using the official `expo-modules-core` from npm, which this package
 * declares as a dependency so both coexist in node_modules.
 *
 * Only the API surface actually imported by the gl-react dependency graph
 * is provided (verified — no other consumers):
 *  - `Platform`                    expo-asset (Asset.ts / AssetSources.ts / AssetSourceResolver.ts)
 *  - `requireNativeModule`         expo-asset (ExpoAsset.ts, evaluated at import time;
 *                                  PlatformUtils.ts probes 'ExpoGo' inside try/catch)
 *  - `requireOptionalNativeModule` expo-asset (PlatformUtils.ts probes 'ExpoUpdates')
 *  - `NativeModulesProxy`          webgltexture-loader-expo
 *
 * TurboModule lookup mirrors the proven ExpoGL spec
 * (@react-native-ohos/expo-gl/src/specs/v1/NativeExpoGL.ts):
 * TurboModuleRegistry.get(name) from 'react-native' (harmony-aliased to
 * @react-native-oh/react-native-harmony), null when not registered.
 */
import { NativeModules, Platform, TurboModuleRegistry } from 'react-native';
import type { TurboModule } from 'react-native/Libraries/TurboModule/RCTExport';

function getNativeModule<T extends TurboModule>(moduleName: string): T | null {
  try {
    // RNOH returns null for unregistered modules; the try/catch keeps the
    // probe safe even if that ever changes to throwing.
    return TurboModuleRegistry.get<T>(moduleName) ?? null;
  } catch {
    return null;
  }
}

/**
 * Proxy over the TurboModule registry. Accessing an unregistered module
 * MUST yield `undefined` (never throw): the deprecated Expo GLObject
 * loader reads `NativeModulesProxy.ExponentGLObjectManager?.createObjectAsync`
 * at module top level and disables itself when it gets undefined.
 */
export const NativeModulesProxy: Record<string, any> = new Proxy(
  {},
  {
    get(_target, prop) {
      if (typeof prop !== 'string') {
        return undefined;
      }
      // Legacy fallback for old NativeModules-registered modules.
      return getNativeModule(prop) ?? (NativeModules as Record<string, any>)[prop];
    },
  }
);

export function requireNativeModule<T extends TurboModule = TurboModule>(
  moduleName: string
): T {
  const nativeModule = getNativeModule<T>(moduleName);
  if (nativeModule == null) {
    // Throwing is intentional: PlatformUtils.ts relies on try/catch around
    // requireNativeModule('ExpoGo') to detect absence.
    throw new Error(
      `Cannot find native module '${moduleName}'. ` +
        `Make sure its RNPackage is registered in RNOHPackagesFactory.`
    );
  }
  return nativeModule;
}

export function requireOptionalNativeModule<T extends TurboModule = TurboModule>(
  moduleName: string
): T | null {
  return getNativeModule<T>(moduleName);
}

export { Platform };
