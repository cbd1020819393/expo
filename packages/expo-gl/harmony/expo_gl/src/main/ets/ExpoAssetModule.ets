/**
 * ExpoAsset TurboModule for the gl-react HarmonyOS dependency chain.
 *
 * Materializes assets into the app's cache directory and hands back a
 * `file://` uri — the only local-URI scheme EXGLImageUtils::loadImage
 * accepts on the GL texture path (cpp/exgl/EXGLImageUtils.cpp).
 *
 * Semantics follow the native contract declared in
 * src/specs/v1/NativeExpoAsset.ts:
 *   downloadAsync(uri, md5 | null, type) → { uri }
 * (width/height probing happens on the JS side via Image.getSize.)
 *
 * Ported from renbaoshuo/expo-harmony's ExpoAssetModule.ets (MIT,
 * Copyright 2026-present Baoshuo) — rewritten from the @expo-harmony
 * expo-modules-core DSL into an RNOH TurboModule. The runtime-shutdown
 * plumbing of the original is intentionally not carried over: an RNOH
 * TurboModule lives as long as the RN instance, so there is no
 * destroy/reload notification to react to. The in-flight request
 * deduplication is kept.
 *
 * Cache layout mirrors Android: <cacheDir>/ExponentAsset-<key>.<ext>,
 * where <key> is the caller-supplied MD5 (validated) or an MD5 of the
 * uri. Finished downloads land via an atomic rename of a uniquely
 * named temp file.
 */
import { TurboModule, UITurboModuleContext } from '@rnoh/react-native-openharmony';
import fs from '@ohos.file.fs';
import fileHash from '@ohos.file.hash';
import request from '@ohos.request';
import cryptoFramework from '@ohos.security.cryptoFramework';
import util from '@ohos.util';
import { TM } from './generated/ts';

interface DownloadResult {
  uri: string;
}

class ExpoAssetError extends Error {
  readonly code: string;

  constructor(code: string, message: string) {
    super(message);
    this.name = 'ExpoAssetError';
    this.code = code;
  }
}

export class ExpoAssetModule extends TurboModule implements TM.ExpoAsset.Spec {
  private readonly inflight: Map<string, Promise<string>> = new Map();

  constructor(ctx: UITurboModuleContext) {
    super(ctx);
  }

  async downloadAsync(uri: string, md5: string | null, type: string): Promise<DownloadResult> {
    return { uri: await this.download(uri, md5, type) };
  }

  private async download(uri: string, md5: string | null, type: string): Promise<string> {
    const scheme = uriScheme(uri);
    if (scheme === 'file') {
      return uri;
    }

    const ext = safeAssetType(type);
    const key = md5 === null ? md5String(uri) : safeMd5(md5);

    const cache = this.ctx.uiAbilityContext.cacheDir.replace(/\/$/, '');
    const file = `${cache}/ExponentAsset-${key}.${ext}`;

    const pending = this.inflight.get(file);
    if (pending !== undefined) return await pending;

    const task = this.materialize(uri, md5, file);
    task.catch((_: Error): void => {});
    this.inflight.set(file, task);

    try {
      return await task;
    } finally {
      this.inflight.delete(file);
    }
  }

  private async materialize(uri: string, md5: string | null, file: string): Promise<string> {
    let cached = false;
    try {
      cached = (await fs.access(file)) && (await this.matches(file, md5));
    } catch (_) {}

    if (cached) return fileUriFromPath(file);

    const temp = `${file}.expo-${Date.now()}-${Math.floor(Math.random() * 0x7fffffff)}.tmp`;
    try {
      if (isRawResource(uri)) await this.copyRawResource(uri, temp);
      else await this.downloadHttp(uri, temp);

      await fs.moveFile(temp, file, 0);

      return fileUriFromPath(file);
    } catch (error) {
      try {
        await fs.unlink(temp);
      } catch (_) {}

      if (error instanceof ExpoAssetError) {
        throw new ExpoAssetError(error.code, error.message);
      }

      throw new ExpoAssetError('ERR_ASSET_DOWNLOAD_FAILED', cleanErrorMessage(error as Object));
    }
  }

  private async matches(path: string, md5: string | null): Promise<boolean> {
    return md5 === null || (await fileHash.hash(path, 'md5')).toLowerCase() === md5.toLowerCase();
  }

  private async copyRawResource(uri: string, file: string): Promise<void> {
    const name = rawResourceName(uri);
    const resources = this.ctx.uiAbilityContext.resourceManager;
    let opened = false;

    try {
      const desc = await resources.getRawFd(name);
      opened = true;

      const output = await fs.open(file, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
      try {
        fs.lseek(desc.fd, desc.offset, fs.WhenceType.SEEK_SET);
        fs.lseek(output.fd, 0, fs.WhenceType.SEEK_SET);

        const buffer = new ArrayBuffer(64 * 1024);
        let written = 0;

        while (written < desc.length) {
          const size = Math.min(buffer.byteLength, desc.length - written);
          const count = await fs.read(desc.fd, buffer, { length: size });

          if (count <= 0) {
            throw new ExpoAssetError('ERR_ASSET_RESOURCE_UNAVAILABLE', `Cannot read the complete packaged asset '${name}'.`);
          }

          await writeAllAsync(output.fd, count === buffer.byteLength ? buffer : buffer.slice(0, count));

          written += count;
        }
      } finally {
        await fs.close(output.fd);
      }
    } catch (error) {
      if (error instanceof ExpoAssetError) {
        throw new ExpoAssetError(error.code, error.message);
      }

      throw new ExpoAssetError('ERR_ASSET_RESOURCE_UNAVAILABLE', `Cannot read packaged asset '${name}': ${cleanErrorMessage(error as Object)}`);
    } finally {
      if (opened) {
        try {
          await resources.closeRawFd(name);
        } catch (error) {
          console.warn(`Unable to close packaged Expo asset '${name}': ${cleanErrorMessage(error as Object)}`);
        }
      }
    }
  }

  private async downloadHttp(uri: string, file: string): Promise<void> {
    const scheme = uriScheme(uri);
    if (scheme !== 'http' && scheme !== 'https') {
      throw new ExpoAssetError('ERR_ASSET_UNSUPPORTED_URI', sanitizedUri(uri));
    }

    let task: request.DownloadTask;
    try {
      task = await request.downloadFile(this.ctx.uiAbilityContext, {
        url: uri,
        filePath: file,
        enableMetered: true,
        enableRoaming: true,
        background: false,
      });
    } catch (error) {
      throw new ExpoAssetError(
        'ERR_ASSET_DOWNLOAD_FAILED',
        `Unable to start asset download from '${sanitizedUri(uri)}': ${cleanErrorMessage(error as Object)}`,
      );
    }

    await this.waitForDownload(task, uri);
  }

  private async waitForDownload(task: request.DownloadTask, uri: string): Promise<void> {
    await new Promise<void>((resolve: () => void, reject: (error: Error) => void): void => {
      let settled = false;

      const cleanup = (): void => {
        try {
          task.off('complete', complete);
          task.off('fail', fail);
        } catch (_) {}
      };

      const settle = (error?: ExpoAssetError): void => {
        if (settled) return;

        settled = true;
        cleanup();

        if (error === undefined) resolve();
        else reject(error as Error);
      };

      const complete = (): void => settle();
      const fail = (reason: number): void =>
        settle(new ExpoAssetError('ERR_ASSET_DOWNLOAD_FAILED', `Unable to download asset from '${sanitizedUri(uri)}' (Harmony reason ${reason}).`));

      try {
        task.on('complete', complete);
        task.on('fail', fail);

        // The download may finish before its listeners are registered.
        task
          .getTaskInfo()
          .then((info: request.DownloadInfo): void => {
            if (info.status === request.SESSION_SUCCESSFUL) complete();
            else if (info.status === request.SESSION_FAILED) fail(info.failedReason);
          })
          .catch((error: Object): void => {
            settle(new ExpoAssetError('ERR_ASSET_DOWNLOAD_FAILED', cleanErrorMessage(error)));
          });
      } catch (error) {
        settle(new ExpoAssetError('ERR_ASSET_DOWNLOAD_FAILED', cleanErrorMessage(error as Object)));
      }
    });
  }
}

function isRawResource(uri: string): boolean {
  const scheme = uriScheme(uri);
  return scheme === 'asset' || scheme === 'rawfile' || scheme === undefined;
}

function rawResourceName(uri: string): string {
  const scheme = uriScheme(uri);
  const rnoh = scheme === 'asset';
  let path = scheme === undefined ? uri : uri.slice(uri.indexOf(':') + 1).replace(/^\/{2}/, '');
  path = path.replace(/^\/+/, '');

  // RNOH asset:// paths are literal; only rawfile:// uses URL encoding.
  if (scheme === 'rawfile') {
    const encoded = path.split(/[?#]/, 1)[0];
    if (/%(?:00|2f|5c)/i.test(encoded)) {
      throw new ExpoAssetError('ERR_ASSET_INVALID_RESOURCE', sanitizedUri(uri));
    }

    try {
      path = decodeURIComponent(encoded);
    } catch (_) {
      throw new ExpoAssetError('ERR_ASSET_INVALID_RESOURCE', sanitizedUri(uri));
    }
  }

  const parts = path.split('/');
  if (path.includes('\\') || parts.some((part: string): boolean => part.length === 0 || part === '.' || part === '..' || part.includes('\0'))) {
    throw new ExpoAssetError('ERR_ASSET_INVALID_RESOURCE', sanitizedUri(uri));
  }

  const name = parts.join('/');
  return rnoh ? `assets/${name}` : name;
}

async function writeAllAsync(fd: number, data: ArrayBuffer): Promise<number> {
  let written = 0;

  while (written < data.byteLength) {
    const end = Math.min(written + 64 * 1024, data.byteLength);
    const count = await fs.write(fd, data.slice(written, end), { length: end - written });
    if (count <= 0) {
      throw new ExpoAssetError('ERR_ASSET_WRITE_FAILED', 'Unable to write the complete asset.');
    }

    written += count;
  }

  return written;
}

function fileUriFromPath(path: string): string {
  // EXGLImageUtils::loadImage strips "file://" and runs its own URI
  // decoding, so percent-encode every path segment (keeping '/') —
  // encodeURIComponent covers spaces, '+', non-ASCII, etc.
  const segments = path.replace(/^\//, '').split('/');
  return `file:///${segments.map((segment: string): string => encodeURIComponent(segment)).join('/')}`;
}

function safeAssetType(type: string): string {
  if (type.length > 64 || (type.length > 0 && !/^[A-Za-z0-9][A-Za-z0-9._+-]*$/.test(type))) {
    throw new ExpoAssetError('ERR_ASSET_INVALID_TYPE', type);
  }

  return type;
}

function safeMd5(value: string): string {
  if (!/^[A-Fa-f0-9]{32}$/.test(value)) {
    throw new ExpoAssetError('ERR_ASSET_INVALID_HASH', 'Expo Asset expected a 32-character hexadecimal MD5 hash.');
  }

  return value.toLowerCase();
}

function uriScheme(value: string): string | undefined {
  const match = /^([A-Za-z][A-Za-z0-9+.-]*):/.exec(value);
  return match === null ? undefined : match[1].toLowerCase();
}

function sanitizedUri(value: string): string {
  const uri = value.split(/[?#]/, 1)[0];
  const scheme = uri.indexOf('://');
  if (scheme < 0) return uri;

  const start = scheme + 3;
  const slash = uri.indexOf('/', start);
  const end = slash < 0 ? uri.length : slash;
  const host = uri.slice(start, end);
  const at = host.lastIndexOf('@');

  return `${uri.slice(0, start)}${at < 0 ? host : host.slice(at + 1)}${uri.slice(end)}`;
}

function cleanErrorMessage(error: Object): string {
  if (error instanceof ExpoAssetError) return error.message;
  return String(error).replace(/^(?:Error:\s*)+/, '');
}

function md5String(text: string): string {
  const digest = cryptoFramework.createMd('MD5');
  if (text.length > 0) {
    digest.updateSync({ data: new util.TextEncoder('utf-8').encodeInto(text) });
  }

  const bytes = digest.digestSync().data;
  let result = '';
  for (let i = 0; i < bytes.length; i += 1) {
    result += bytes[i].toString(16).padStart(2, '0');
  }

  return result;
}
