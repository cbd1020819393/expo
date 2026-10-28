/**
 * HarmonyOS (RNOH) entry for @react-native-ohos/expo-asset.
 *
 * This package only ever resolves on the harmony platform (via the
 * `harmony.alias` redirect in RNOH's metro resolver). Android/iOS keep
 * using the official `expo-asset` from npm, which this package declares
 * as a dependency so both coexist in node_modules.
 *
 * Why a hand-written entry instead of re-exporting the official JS:
 * the official expo-asset@58 build imports `expo/internal/bundle-origin`
 * (inside the `expo` package itself), which customer apps don't install —
 * metro would fail to resolve it. This entry implements exactly the API
 * surface consumed by the gl-react dependency graph (verified — the only
 * JS consumer in scope is webgltexture-loader-expo):
 *  - `Asset.fromModule(...)` / `Asset.fromURI(...)`
 *  - `asset.downloadAsync()` → fills `localUri` / `width` / `height`
 *  - `Asset.loadAsync(...)`
 *
 * Files are materialized by the native `ExpoAsset` TurboModule
 * (asset:// → rawfile copy, http(s) → download, file:// passthrough).
 * Native contract (mirrors the official shape):
 *   downloadAsync(uri: string, md5: string | null, type: string)
 *     → Promise<{ uri: string; width?: number; height?: number }>
 *
 * `requireNativeModule('ExpoAsset')` is called lazily inside
 * downloadAsync, so importing this module stays safe even before the
 * native side is registered (the failure surfaces as a clear error on
 * first texture download instead of an app crash at import time).
 */
import { Image } from 'react-native';
import { requireNativeModule } from 'expo-modules-core';

interface ExpoAssetNativeModule {
  downloadAsync(
    uri: string,
    md5: string | null,
    type: string
  ): Promise<{ uri: string; width?: number; height?: number }>;
}

interface AssetOptions {
  uri: string;
  width?: number;
  height?: number;
  hash?: string;
  type?: string;
}

/**
 * Derive the cache file extension from the uri, clamped to a safe
 * charset (same validation the native side applies to `type`).
 */
function getExtension(uri: string): string {
  const path = uri.split(/[?#]/, 1)[0];
  const dot = path.lastIndexOf('.');
  const slash = path.lastIndexOf('/');
  if (dot === -1 || dot <= slash || dot === path.length - 1) {
    return 'bin';
  }
  const ext = path.slice(dot + 1);
  return /^[A-Za-z0-9][A-Za-z0-9._+-]*$/.test(ext) && ext.length <= 64
    ? ext
    : 'bin';
}

/**
 * Probe image dimensions for the downloaded file. Best-effort: resolves
 * to null for non-image assets or unsupported formats — consumers
 * (webgltexture-loader-expo) fall back to their own Image.getSize path.
 */
function probeImageSize(uri: string): Promise<{ width: number; height: number } | null> {
  return new Promise((resolve) => {
    try {
      Image.getSize(
        uri,
        (width: number, height: number) => resolve({ width, height }),
        () => resolve(null)
      );
    } catch {
      resolve(null);
    }
  });
}

export class Asset {
  uri: string;
  localUri?: string;
  width?: number;
  height?: number;
  hash?: string;
  type: string;
  downloaded = false;

  constructor(options: AssetOptions) {
    this.uri = options.uri;
    this.width = options.width;
    this.height = options.height;
    this.hash = options.hash;
    this.type = options.type ?? getExtension(options.uri);
  }

  async downloadAsync(): Promise<Asset> {
    if (!this.downloaded) {
      const native = requireNativeModule<ExpoAssetNativeModule>('ExpoAsset');
      const result = await native.downloadAsync(
        this.uri,
        this.hash ?? null,
        this.type
      );
      this.localUri = result.uri;
      if (this.width == null && result.width != null) {
        this.width = result.width;
      }
      if (this.height == null && result.height != null) {
        this.height = result.height;
      }
      if (this.width == null || this.height == null) {
        const size = await probeImageSize(this.localUri);
        if (size != null) {
          if (this.width == null) this.width = size.width;
          if (this.height == null) this.height = size.height;
        }
      }
      this.downloaded = true;
    }
    return this;
  }

  static fromURI(uri: string): Asset {
    return new Asset({ uri });
  }

  static fromModule(moduleIdOrUri: number | string): Asset {
    if (typeof moduleIdOrUri === 'string') {
      return Asset.fromURI(moduleIdOrUri);
    }
    const source = Image.resolveAssetSource(moduleIdOrUri);
    if (source == null || typeof source.uri !== 'string') {
      throw new Error(
        `Failed to resolve asset source for module '${moduleIdOrUri}'.`
      );
    }
    return new Asset({
      uri: source.uri,
      width: source.width,
      height: source.height,
    });
  }

  static async loadAsync(
    moduleIdOrUriOrArray: (number | string) | Array<number | string>
  ): Promise<Asset[]> {
    const modules = Array.isArray(moduleIdOrUriOrArray)
      ? moduleIdOrUriOrArray
      : [moduleIdOrUriOrArray];
    return Promise.all(
      modules.map((module) => Asset.fromModule(module).downloadAsync())
    );
  }
}
