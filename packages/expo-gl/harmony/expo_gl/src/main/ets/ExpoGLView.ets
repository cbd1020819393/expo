/**
 * HarmonyOS (RNOH) implementation of the ExpoGL native view.
 *
 * Renders an XComponent (surface type) backed by `libexpo_gl.so`, which owns
 * the EGL context and the EXGL render thread. Once the surface is live and
 * the GL context has been prepared on the JS runtime, the native side notifies
 * the listener registered below and the view emits `onSurfaceCreate` with
 * `{ exglCtxId }` — the same event the upstream TextureView/CALayer views
 * fire on Android/iOS.
 */
import { RNOHContext, RNViewBase } from '@rnoh/react-native-openharmony';
import exglNative from 'libexpo_gl.so';
import { RNC } from './generated/ts';


const XCOMPONENT_ID_PREFIX = 'exgl_';
const XCOMPONENT_LIBRARY = 'expo_gl';

let listenerRegistered = false;
const surfaceReadyHandlers = new Map<string, (exglCtxId: number) => void>();

function ensureSurfaceListener(): void {
  if (listenerRegistered) {
    return;
  }
  listenerRegistered = true;
  exglNative.registerSurfaceListener((xcomponentId: string, exglCtxId: number) => {
    const handler = surfaceReadyHandlers.get(xcomponentId);
    if (handler !== undefined) {
      surfaceReadyHandlers.delete(xcomponentId);
      handler(exglCtxId);
    }
  });
}

@Component
export struct ExpoGLView {
  static readonly NAME = RNC.ExpoGLView.NAME;
  public ctx!: RNOHContext;
  public tag: number = 0;
  @State descriptorWrapper: RNC.ExpoGLView.DescriptorWrapper = {} as RNC.ExpoGLView.DescriptorWrapper;
  private eventEmitter: RNC.ExpoGLView.EventEmitter | undefined = undefined;
  private cleanUpCallbacks: (() => void)[] = [];
  private xcomponentId: string = '';
  private mXComponentController: XComponentController = new XComponentController();
  private surfacePrepared: boolean = false;

  aboutToAppear(): void {
    console.info('[ExpoGLProbe] ArkTS: ExpoGLView aboutToAppear, tag=' + this.tag);
    ensureSurfaceListener();
    this.xcomponentId = `${XCOMPONENT_ID_PREFIX}${this.tag}`;
    this.eventEmitter = new RNC.ExpoGLView.EventEmitter(this.ctx.rnInstance, this.tag);

    const descriptorWrapper = this.ctx.descriptorRegistry
      .findDescriptorWrapperByTag<RNC.ExpoGLView.DescriptorWrapper>(this.tag);
    if (descriptorWrapper !== null && descriptorWrapper !== undefined) {
      this.descriptorWrapper = descriptorWrapper;
    }
    this.cleanUpCallbacks.push(this.ctx.descriptorRegistry.subscribeToDescriptorChanges(this.tag,
      (descriptor) => {
        this.descriptorWrapper = new RNC.ExpoGLView.DescriptorWrapper(descriptor);
      }
    ));

    // Provide the sandbox cache directory (used by takeSnapshotAsync to write
    // GLView/*.png|jpeg files). `msaaSamples` is iOS-only upstream and
    // `enableExperimentalWorkletSupport` is handled on the JS side, so no
    // prop plumbing is needed here.
    const uiAbilityContext = this.ctx.uiAbilityContext;
    if (uiAbilityContext !== undefined) {
      exglNative.setCacheDir(uiAbilityContext.cacheDir);
    }

    surfaceReadyHandlers.set(this.xcomponentId, (exglCtxId: number) => {
      console.info('[ExpoGLProbe] ArkTS: surface ready, emitting surfaceCreate, exglCtxId=' + exglCtxId);
      this.eventEmitter?.emit('surfaceCreate', { exglCtxId });
    });

    // The surfaceId is normally available as soon as the XComponent `onLoad`
    // below fires; keep a short-interval polling fallback for embeds where
    // onLoad is never delivered.
    this.pollSurfaceReady();
  }

  private pollSurfaceReady(attempt: number = 0): void {
    if (this.surfacePrepared) {
      return;
    }
    if (attempt >= 600) {
      console.error('[ExpoGLProbe] ArkTS: surfaceId/size never became available');
      return;
    }
    let surfaceWidth: number = 0;
    let surfaceHeight: number = 0;
    try {
      const surfaceId = this.mXComponentController.getXComponentSurfaceId();
      try {
        const rect = this.mXComponentController.getXComponentSurfaceRect();
        if (rect !== undefined && rect !== null) {
          surfaceWidth = rect.surfaceWidth;
          surfaceHeight = rect.surfaceHeight;
        }
      } catch (e) {
        // Size not available yet — keep polling.
      }
      if (surfaceId !== undefined && surfaceId !== null && surfaceId.length > 0
        && surfaceWidth > 0 && surfaceHeight > 0) {
        console.info('[ExpoGLProbe] ArkTS: ready (poll ' + attempt + '): surfaceId=' +
          surfaceId + ', size=' + surfaceWidth + 'x' + surfaceHeight);
        this.surfacePrepared = true;
        this.prepareSurfaceNative(surfaceId, surfaceWidth, surfaceHeight);
        return;
      }
    } catch (e) {
      // Controller not bound to a surface yet — keep polling.
    }
    setTimeout(() => this.pollSurfaceReady(attempt + 1), 16);
  }

  // The native side must SET the producer window buffer geometry itself:
  // windows created via OH_NativeWindow_CreateNativeWindowFromSurfaceId carry
  // no geometry (GET_BUFFER_GEOMETRY returns 0x0 forever), which makes
  // eglSwapBuffers fail with EGL_BAD_ALLOC. Hand over the XComponent pixel
  // size observed here so the native side can size the window before EGL
  // touches it.
  private prepareSurfaceNative(surfaceId: string, width: number, height: number): void {
    exglNative.prepareSurface(surfaceId, this.xcomponentId, width, height);
  }

  aboutToDisappear(): void {
    this.cleanUpCallbacks.forEach(cb => cb());
    surfaceReadyHandlers.delete(this.xcomponentId);
    // The controller-based XComponent never delivers the native
    // OnSurfaceDestroyed callback, so release the GL context here — without
    // this call every mount leaks a GL thread + EGL context + native window.
    if (this.surfacePrepared) {
      this.surfacePrepared = false;
      exglNative.destroySurface(this.xcomponentId);
    }
  }

  build() {
    RNViewBase({ ctx: this.ctx, tag: this.tag }) {
      // Use the controller-based XComponent API (no `library` handshake): the
      // ArkTS import of libexpo_gl.so races against XComponent creation and
      // permanently loses the OH_NativeXComponent delivery. Instead the
      // surfaceId + component pixel size are handed to the native side from
      // pollSurfaceReady().
      XComponent({
        id: this.xcomponentId,
        type: 'surface',
        controller: this.mXComponentController
      })
        .onLoad(() => {
          // surfaceId/size are typically ready the moment onLoad fires —
          // try immediately instead of waiting for the polling chain.
          this.pollSurfaceReady();
        })
        .width('100%')
        .height('100%')
    }
  }
}
