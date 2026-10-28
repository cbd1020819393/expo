/**
 * TurboModule Spec for the ExpoAsset native module (HarmonyOS / RNOH).
 *
 * Consumed through the hand-written `@react-native-ohos/expo-asset` JS
 * entry (expo-asset/harmony/index.ts) via `requireNativeModule('ExpoAsset')`
 * — this file is the codegen source for the ArkTS binding
 * (`npm run codegen-lib`) and documents the native contract:
 *
 *  - `file://` uris pass through untouched
 *  - `asset://` / `rawfile://` / bare paths stream out of the packaged
 *    rawfile resources (RNOH `asset://` paths map under `assets/`)
 *  - `http(s)://` uris are downloaded into the app cache
 *
 * The result is always a `file://` uri — the only local-URI scheme the
 * GL texture path (EXGLImageUtils::loadImage) accepts. Image dimensions
 * are probed on the JS side (Image.getSize fallback), hence optional.
 */
import type { TurboModule } from 'react-native/Libraries/TurboModule/RCTExport';
import { TurboModuleRegistry } from 'react-native';

export interface DownloadResult {
  uri: string;
  width?: number;
  height?: number;
}

export interface Spec extends TurboModule {
  downloadAsync(uri: string, md5: string | null, type: string): Promise<DownloadResult>;
}

export default TurboModuleRegistry.get<Spec>('ExpoAsset') as Spec | null;
