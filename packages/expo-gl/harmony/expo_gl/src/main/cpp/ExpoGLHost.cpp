#include "ExpoGLHost.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <future>

#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <jsi/jsi.h>
#include <ReactCommon/CallInvoker.h>

#include <multimedia/image_framework/image/image_common.h>
#include <multimedia/image_framework/image/image_packer_native.h>
#include <multimedia/image_framework/image/pixelmap_native.h>

#include "ExpoGLNapi.h"
#include "exgl/EXGLNativeApi.h"
#include "exgl/EXPlatformUtils.h"

namespace facebook {
namespace react {
class CallInvoker;
} // namespace react
} // namespace facebook

namespace expo {
namespace gl_cpp {

namespace jsi = facebook::jsi;

// ---------------------------------------------------------------------------
// GLHostContext
// ---------------------------------------------------------------------------

GLHostContext::GLHostContext(std::string xcomponentId, void *nativeWindow)
  : xcomponentId_(std::move(xcomponentId)), nativeWindow_(nativeWindow) {}

GLHostContext::~GLHostContext() {
  if (thread_.joinable()) {
    stopAndJoin();
  }
}

void GLHostContext::start() {
  thread_ = std::thread([this] { threadMain(); });
}

void GLHostContext::enqueue(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    tasks_.push_back(std::move(task));
  }
  queueCv_.notify_one();
}

void GLHostContext::enqueueAndWait(std::function<void()> task) {
  std::packaged_task<void(void)> packaged(std::move(task));
  auto future = packaged.get_future();
  enqueue([&packaged] { packaged(); });
  future.wait();
}

void GLHostContext::stopAndJoin() {
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    stopping_ = true;
  }
  queueCv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool GLHostContext::initEGL() {
  display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display_ == EGL_NO_DISPLAY) {
    EXGLSysLog("EXGL: eglGetDisplay failed");
    return false;
  }
  EGLint major = 0, minor = 0;
  if (!eglInitialize(display_, &major, &minor)) {
    EXGLSysLog("EXGL: eglInitialize failed");
    return false;
  }

  // Mirrors the Android config spec: 8888 RGB + alpha, 16-bit depth, 8-bit
  // stencil, usable with OpenGL ES.
  const EGLint configAttribs[] = {
    EGL_SURFACE_TYPE,
    EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE,
    EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 16,
    EGL_STENCIL_SIZE, 8,
    EGL_NONE,
  };
  EGLint numConfigs = 0;
  if (!eglChooseConfig(display_, configAttribs, &config_, 1, &numConfigs) || numConfigs < 1) {
    EXGLSysLog("EXGL: eglChooseConfig failed");
    return false;
  }

  const EGLint contextAttribs3[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  const EGLint contextAttribs2[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  eglContext_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs3);
  if (eglContext_ == EGL_NO_CONTEXT) {
    eglClientVersion_ = 2;
    eglContext_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs2);
    if (eglContext_ == EGL_NO_CONTEXT) {
      EXGLSysLog("EXGL: eglCreateContext failed");
      return false;
    }
  }

  if (nativeWindow_ != nullptr) {
    surface_ = eglCreateWindowSurface(
      display_, config_, reinterpret_cast<EGLNativeWindowType>(nativeWindow_), nullptr);
  } else {
    // Same as Android: some devices crash when the pbuffer has no explicit
    // dimensions.
    const EGLint pbufferAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    surface_ = eglCreatePbufferSurface(display_, config_, pbufferAttribs);
  }
  if (surface_ == EGL_NO_SURFACE) {
    EXGLSysLog("EXGL: failed to create EGL surface (0x%x)", eglGetError());
    return false;
  }

  if (!eglMakeCurrent(display_, surface_, surface_, eglContext_)) {
    EXGLSysLog("EXGL: eglMakeCurrent failed (0x%x)", eglGetError());
    return false;
  }

  // Enable buffer preservation — allows apps to draw over previous frames
  // without clearing (same as Android GLThread.initEGL).
  eglSurfaceAttrib(display_, surface_, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);
  return true;
}

void GLHostContext::deinitEGL() {
  // Runs on the GL thread, where the context is current.
  if (display_ != EGL_NO_DISPLAY) {
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface_ != EGL_NO_SURFACE) {
      eglDestroySurface(display_, surface_);
      surface_ = EGL_NO_SURFACE;
    }
    if (eglContext_ != EGL_NO_CONTEXT) {
      eglDestroyContext(display_, eglContext_);
      eglContext_ = EGL_NO_CONTEXT;
    }
    // [B-7] No eglTerminate here: every host resolves the same process-wide
    // default display via eglGetDisplay(EGL_DEFAULT_DISPLAY), so terminating
    // it would invalidate the surfaces/contexts of sibling GL views
    // (multi-view apps crash with "Cannot swap buffers!"). The process
    // reclaims the display at exit.
    display_ = EGL_NO_DISPLAY;
  }
}

void GLHostContext::threadMain() {
  if (!initEGL()) {
    EXGLSysLog("EXGL: GL thread exiting, EGL init failed for context %u", exglCtxId);
    // [B-16] Release whatever EGL resources initEGL managed to create before
    // failing (it aborts at the first failed step).
    deinitEGL();
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      stopping_ = true;
    }
    queueCv_.notify_all();
    return;
  }

  while (true) {
    std::deque<std::function<void()>> batch;
    {
      std::unique_lock<std::mutex> lock(queueMutex_);
      queueCv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
      batch.swap(tasks_);
    }
    for (auto &task : batch) {
      task();
    }
    batch.clear();

    if (exglCtxId != 0) {
      // Run one frame's worth of queued WebGL commands.
      EXGLContextFlush(exglCtxId);
      if (!isHeadless() && EXGLContextNeedsRedraw(exglCtxId)) {
        EGLBoolean swapped = eglSwapBuffers(display_, surface_);
        EXGLSysLog("EXGL: swap ret=%d err=0x%{public}x surface=%p", (int)swapped,
            (unsigned)eglGetError(), (void *)surface_);
        if (!swapped) {
          EXGLSysLog("EXGL: Cannot swap buffers! (0x%{public}x)", eglGetError());
        }
        EXGLContextDrawEnded(exglCtxId);
      }
    }

    if (stopping_) {
      break;
    }
  }

  deinitEGL();
}

// ---------------------------------------------------------------------------
// Host state
// ---------------------------------------------------------------------------

namespace {

struct HostState {
  std::mutex mutex;
  std::unordered_map<std::string, std::shared_ptr<GLHostContext>> surfaceHosts;
  std::unordered_map<EXGLContextId, std::shared_ptr<GLHostContext>> hostsById;
  std::unordered_map<EXGLObjectId, std::function<void()>> glObjects;

  void *jsiRuntime = nullptr;
  std::shared_ptr<facebook::react::CallInvoker> callInvoker;
  std::vector<std::shared_ptr<GLHostContext>> pendingPrepares;

  std::string cacheDir = "/data/storage/el2/base/haps/entry/cache";
};

HostState &hostState() {
  static HostState state;
  return state;
}

void prepareHostOnJSThread(
  jsi::Runtime &runtime,
  std::shared_ptr<GLHostContext> host) {
  EXGLInstallWebGLBindings(&runtime);
  auto flushMethod = [host] { host->enqueue([] {}); };
  EXGLContextPrepare(&runtime, host->exglCtxId, flushMethod);
  HostNotifySurfaceReady(host->xcomponentId(), host->exglCtxId);
}

} // namespace

void HostSetJsiRuntime(
  void *runtime,
  std::shared_ptr<facebook::react::CallInvoker> invoker) {
  auto &state = hostState();
  std::lock_guard<std::mutex> lock(state.mutex);
  // [A-3] A replaced runtime invalidates queued surface prepares — their
  // invokeAsync callbacks would run against a dead runtime.
  if (state.jsiRuntime != nullptr && state.jsiRuntime != runtime
      && !state.pendingPrepares.empty()) {
    EXGLSysLog(
      "EXGL: JS runtime replaced while %zu surface prepares pending; dropping them",
      state.pendingPrepares.size());
    state.pendingPrepares.clear();
  }
  state.jsiRuntime = runtime;
  state.callInvoker = std::move(invoker);
}

void *HostGetJsiRuntime() {
  auto &state = hostState();
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.jsiRuntime;
}

std::shared_ptr<facebook::react::CallInvoker> HostGetCallInvoker() {
  auto &state = hostState();
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.callInvoker;
}

void HostSetCacheDir(const std::string &dir) {
  if (dir.empty()) {
    return;
  }
  auto &state = hostState();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.cacheDir = dir;
}

std::string HostGetCacheDir() {
  auto &state = hostState();
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.cacheDir;
}

void HostNotifySurfaceReady(const std::string &xcomponentId, EXGLContextId ctxId) {
  // Implemented via the napi threadsafe function in ExpoGLNapi.cpp.
  NapiNotifySurfaceReady(xcomponentId, ctxId);
}

void HostPrepareSurfaceContext(std::shared_ptr<GLHostContext> host) {
  auto &state = hostState();
  std::shared_ptr<facebook::react::CallInvoker> invoker;
  void *runtime = nullptr;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.pendingPrepares.empty() && state.jsiRuntime != nullptr && state.callInvoker != nullptr) {
      runtime = state.jsiRuntime;
      invoker = state.callInvoker;
    } else {
      state.pendingPrepares.push_back(host);
    }
  }
  if (runtime == nullptr) {
    // `prepareGLRuntime` hasn't run yet — the pending list is drained from
    // it once the JS bundle evaluates the library entry.
    EXGLSysLog("EXGL: JS runtime not ready; deferring prepare for context %u", host->exglCtxId);
    return;
  }
  auto preparedHost = std::move(host);
  invoker->invokeAsync([runtime, preparedHost] {
    prepareHostOnJSThread(*reinterpret_cast<jsi::Runtime *>(runtime), preparedHost);
  });
}

void DrainPendingSurfacePrepares() {
  auto &state = hostState();
  std::vector<std::shared_ptr<GLHostContext>> pending;
  std::shared_ptr<facebook::react::CallInvoker> invoker;
  void *runtime = nullptr;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.jsiRuntime == nullptr || state.callInvoker == nullptr) {
      return;
    }
    pending.swap(state.pendingPrepares);
    runtime = state.jsiRuntime;
    invoker = state.callInvoker;
  }
  if (pending.empty()) {
    return;
  }
  invoker->invokeAsync([runtime, pending] {
    for (auto &host : pending) {
      prepareHostOnJSThread(*reinterpret_cast<jsi::Runtime *>(runtime), host);
    }
  });
}

// ---------------------------------------------------------------------------
// Surface lifecycle (XComponent callbacks run on the UI thread)
// ---------------------------------------------------------------------------

static void RegisterSurfaceHost(std::string xcomponentId, std::shared_ptr<GLHostContext> host) {
  auto &state = hostState();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.surfaceHosts[xcomponentId] = host;
  state.hostsById[host->exglCtxId] = std::move(host);
}

void HostOnSurfaceCreated(const std::string &xcomponentId, void *window) {
  auto host = std::make_shared<GLHostContext>(xcomponentId, window);
  host->start();
  host->exglCtxId = EXGLContextCreate();
  EXGLSysLog("EXGL: surface %s -> context %u", xcomponentId.c_str(), host->exglCtxId);
  RegisterSurfaceHost(xcomponentId, host);
  HostPrepareSurfaceContext(host);
}

void HostOnSurfaceChanged(
  const std::string &xcomponentId,
  uint32_t width,
  uint32_t height) {
  // Window-backed EGL surfaces resize with the native window; nothing to do
  // beyond logging (viewport tracking matches the upstream Android behavior,
  // which also keeps the initial drawing-buffer size).
  (void)xcomponentId;
  (void)width;
  (void)height;
}

void HostOnSurfaceDestroyed(const std::string &xcomponentId) {
  auto &state = hostState();
  std::shared_ptr<GLHostContext> host;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    auto iter = state.surfaceHosts.find(xcomponentId);
    if (iter == state.surfaceHosts.end()) {
      return;
    }
    host = iter->second;
    state.surfaceHosts.erase(iter);
    state.hostsById.erase(host->exglCtxId);
  }
  EXGLSysLog("EXGL: destroying surface context %u", host->exglCtxId);
  host->stopAndJoin();
  EXGLContextDestroy(host->exglCtxId);
}

std::shared_ptr<GLHostContext> HostFindContextById(EXGLContextId ctxId) {
  auto &state = hostState();
  std::lock_guard<std::mutex> lock(state.mutex);
  auto iter = state.hostsById.find(ctxId);
  if (iter != state.hostsById.end()) {
    return iter->second;
  }
  return nullptr;
}

void HostRegisterHeadlessContext(std::shared_ptr<GLHostContext> host) {
  auto &state = hostState();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.hostsById[host->exglCtxId] = std::move(host);
}

void HostReleaseContext(std::shared_ptr<GLHostContext> host) {
  auto &state = hostState();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.hostsById.erase(host->exglCtxId);
  if (!host->xcomponentId().empty()) {
    state.surfaceHosts.erase(host->xcomponentId());
  }
}

bool HostDestroyObject(EXGLObjectId objId) {
  // Mirrors GLModule.mGLObjects on Android: objects are registered by
  // producers (upstream: GLCameraObject). No producer exists in the first
  // phase, so any id is unknown and the upstream "resolve false" applies.
  auto &state = hostState();
  std::lock_guard<std::mutex> lock(state.mutex);
  auto iter = state.glObjects.find(objId);
  if (iter == state.glObjects.end()) {
    return false;
  }
  auto destroy = std::move(iter->second);
  state.glObjects.erase(iter);
  destroy();
  return true;
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

namespace {

std::string generateSnapshotPath(const std::string &cacheDir, const std::string &extension) {
  static std::atomic<uint32_t> counter{0};
  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  uint32_t seq = counter.fetch_add(1);
  char fileName[64];
  snprintf(fileName, sizeof(fileName), "%llx%04x", (long long)now, seq);

  std::string dir = cacheDir;
  if (!dir.empty() && dir.back() == '/') {
    dir.pop_back();
  }
  dir += "/GLView";
  mkdir(dir.c_str(), 0755);

  return dir + "/" + fileName + extension;
}

bool encodeSnapshotFile(
  const uint8_t *pixels,
  int width,
  int height,
  const std::string &format,
  double compress,
  const std::string &filePath) {
  // webp is downgraded to png on HarmonyOS (same as iOS), see the adaptation
  // notes; jpeg remains the default.
  std::string mimeType;
  if (format == "png") {
    mimeType = "image/png";
  } else if (format == "webp") {
    EXGLSysLog(
      "EXGL: HarmonyOS doesn't reliably support 'webp' snapshot encoding, exporting 'png' instead");
    mimeType = "image/png";
  } else {
    mimeType = "image/jpeg";
  }

  OH_Pixelmap_InitializationOptions *createOptions = nullptr;
  OH_PixelmapNative *pixelmap = nullptr;
  OH_ImagePackerNative *packer = nullptr;
  OH_PackingOptions *packOptions = nullptr;
  bool ok = false;

  do {
    if (OH_PixelmapInitializationOptions_Create(&createOptions) != IMAGE_SUCCESS) {
      break;
    }
    OH_PixelmapInitializationOptions_SetWidth(createOptions, width);
    OH_PixelmapInitializationOptions_SetHeight(createOptions, height);
    OH_PixelmapInitializationOptions_SetPixelFormat(createOptions, PIXEL_FORMAT_RGBA_8888);
    OH_PixelmapInitializationOptions_SetSrcPixelFormat(createOptions, PIXEL_FORMAT_RGBA_8888);

    if (OH_PixelmapNative_CreatePixelmap(
          const_cast<uint8_t *>(pixels),
          static_cast<size_t>(width) * height * 4,
          createOptions,
          &pixelmap) != IMAGE_SUCCESS) {
      break;
    }

    if (OH_ImagePackerNative_Create(&packer) != IMAGE_SUCCESS) {
      break;
    }
    if (OH_PackingOptions_Create(&packOptions) != IMAGE_SUCCESS) {
      break;
    }
    Image_MimeType mime{const_cast<char *>(mimeType.c_str()), mimeType.size()};
    OH_PackingOptions_SetMimeType(packOptions, &mime);
    int quality = static_cast<int>(compress * 100.0);
    if (quality < 0) {
      quality = 0;
    } else if (quality > 100) {
      quality = 100;
    }
    OH_PackingOptions_SetQuality(packOptions, static_cast<uint32_t>(quality));

    int fd = open(filePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
      EXGLSysLog("EXGL: cannot open snapshot file %s", filePath.c_str());
      break;
    }
    Image_ErrorCode packResult = OH_ImagePackerNative_PackToFileFromPixelmap(
      packer, packOptions, pixelmap, fd);
    close(fd);
    if (packResult != IMAGE_SUCCESS) {
      EXGLSysLog("EXGL: PackToFileFromPixelmap failed: %d", static_cast<int>(packResult));
      unlink(filePath.c_str());
      break;
    }
    ok = true;
  } while (false);

  if (packOptions != nullptr) {
    OH_PackingOptions_Release(packOptions);
  }
  if (packer != nullptr) {
    OH_ImagePackerNative_Release(packer);
  }
  if (pixelmap != nullptr) {
    OH_PixelmapNative_Release(pixelmap);
  }
  if (createOptions != nullptr) {
    OH_PixelmapInitializationOptions_Release(createOptions);
  }
  return ok;
}

} // namespace

void HostTakeSnapshot(
  std::shared_ptr<GLHostContext> host,
  SnapshotParams params,
  std::function<void(SnapshotResult)> callback) {
  // The read runs on the GL thread (FIFO behind any pending frames), the
  // encode on a worker thread (mirrors Android's AsyncTask pool) and the
  // callback resolution goes back to the JS thread from the TurboModule.
  auto pixels = std::make_shared<std::vector<uint8_t>>();
  auto meta = std::make_shared<SnapshotResult>();
  // Signals when the GL-thread read finished, so the encode thread never
  // observes a half-written meta.
  auto readDone = std::make_shared<std::promise<void>>();
  auto readDoneFuture = readDone->get_future();

  host->enqueue([host, params, pixels, meta, readDone] {
    // [GL thread]
    do {
      GLint prevFramebuffer = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFramebuffer);

      GLint sourceFramebuffer = host->isHeadless() ? prevFramebuffer : 0;
      if (params.hasFramebuffer) {
        sourceFramebuffer =
          static_cast<GLint>(EXGLContextGetObject(host->exglCtxId, params.framebufferId));
      }
      if (sourceFramebuffer == 0 && host->isHeadless()) {
        meta->errorCode = "E_GL_NO_FRAMEBUFFER";
        meta->errorMessage =
          "No framebuffer bound. Create and bind one to take a snapshot from it.";
        break;
      }

      int x = params.x;
      int y = params.y;
      int width = params.width;
      int height = params.height;
      if (!params.hasRect) {
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        x = viewport[0];
        y = viewport[1];
        width = viewport[2];
        height = viewport[3];
      }
      if (width <= 0 || height <= 0) {
        meta->errorCode = "E_GL_INVALID_VIEWPORT";
        meta->errorMessage =
          "Rect's width and height must be greater than 0. If you didn't set `rect` option, check if the viewport is set correctly.";
        meta->width = width;
        meta->height = height;
        break;
      }

      glBindFramebuffer(GL_FRAMEBUFFER, sourceFramebuffer);
      pixels->resize(static_cast<size_t>(width) * height * 4);
      glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels->data());
      glBindFramebuffer(GL_FRAMEBUFFER, prevFramebuffer);

      if (!params.flip) {
        // glReadPixels returns rows bottom-up; the on-screen orientation needs
        // them top-down. `flip` keeps the raw GL orientation (same semantics as
        // upstream Android/iOS).
        size_t rowBytes = static_cast<size_t>(width) * 4;
        std::vector<uint8_t> tmp(rowBytes);
        for (int rowTop = 0, rowBottom = height - 1; rowTop < rowBottom; rowTop++, rowBottom--) {
          std::memcpy(tmp.data(), pixels->data() + static_cast<size_t>(rowTop) * rowBytes, rowBytes);
          std::memcpy(
            pixels->data() + static_cast<size_t>(rowTop) * rowBytes,
            pixels->data() + static_cast<size_t>(rowBottom) * rowBytes,
            rowBytes);
          std::memcpy(
            pixels->data() + static_cast<size_t>(rowBottom) * rowBytes, tmp.data(), rowBytes);
        }
      }

      meta->ok = true;
      meta->width = width;
      meta->height = height;
    } while (false);
    // Signal on every path so the encode thread can never wait forever.
    readDone->set_value();
  });

  // Encode on a worker thread (mirrors Android's AsyncTask pool) and resolve
  // back on the JS thread.
  std::thread encodeThread(
    [host, params, pixels, meta, readDoneFuture = std::move(readDoneFuture), callback] {
    readDoneFuture.wait();
    SnapshotResult result;
    if (meta->ok) {
      std::string format = params.format.empty() ? "jpeg" : params.format;
      std::string extension = format == "png" || format == "webp" ? ".png" : ".jpeg";
      std::string path = generateSnapshotPath(HostGetCacheDir(), extension);
      if (encodeSnapshotFile(pixels->data(), meta->width, meta->height, format, params.compress, path)) {
        result.ok = true;
        result.width = meta->width;
        result.height = meta->height;
        result.uri = "file://" + path;
      } else {
        result.errorCode = "E_GL_CANT_SAVE_SNAPSHOT";
        result.errorMessage = "Failed to encode or write the snapshot image.";
      }
    } else {
      result.errorCode = meta->errorCode;
      result.errorMessage = meta->errorMessage;
    }
    if (callback) {
      callback(std::move(result));
    }
  });
  encodeThread.detach();
}

PendingSurfaceWindows &GetPendingSurfaceWindows() {
  static PendingSurfaceWindows pending;
  return pending;
}

} // namespace gl_cpp
} // namespace expo
