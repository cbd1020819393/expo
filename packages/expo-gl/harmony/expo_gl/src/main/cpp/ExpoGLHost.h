#pragma once

#include <EGL/egl.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "exgl/EXGLNativeApi.h"

namespace facebook {
namespace react {
class CallInvoker;
} // namespace react
} // namespace facebook

namespace expo {
namespace gl_cpp {

// ---------------------------------------------------------------------------
// GLHostContext
//
// HarmonyOS counterpart of the Android GLContext/GLThread pair
// (android/src/main/java/expo/modules/gl/GLContext.java): a dedicated GL
// thread owns an EGL context and drains a queue of tasks. The WebGL command
// batches queued by the C++ core (EXGLContext) are executed on this thread;
// for on-screen contexts a finished frame is swapped to the native window
// (XComponent surface) when `endFrameEXP()` was called.
// ---------------------------------------------------------------------------

class GLHostContext {
public:
  // `nativeWindow` == nullptr creates a headless context backed by a 1x1
  // pbuffer surface (same as Android passing a null SurfaceTexture).
  GLHostContext(std::string xcomponentId, void *nativeWindow);
  ~GLHostContext();

  void start();

  // [Any thread] Stop the GL thread (after draining queued tasks), tear down
  // EGL on the GL thread and join. The core EXGLContext must be destroyed
  // separately (EXGLContextDestroy) afterwards.
  void stopAndJoin();

  // [Any thread] Schedule a task on the GL thread.
  void enqueue(std::function<void()> task);

  // [Any thread] Schedule a task and wait until it has run.
  void enqueueAndWait(std::function<void()> task);

  bool isHeadless() const { return nativeWindow_ == nullptr; }
  const std::string &xcomponentId() const { return xcomponentId_; }

  EXGLContextId exglCtxId = 0;

private:
  void threadMain();
  bool initEGL();
  void deinitEGL();

  std::string xcomponentId_;
  void *nativeWindow_;

  std::thread thread_;
  std::mutex queueMutex_;
  std::condition_variable queueCv_;
  std::deque<std::function<void()>> tasks_;
  bool stopping_ = false;

  // EGL state, owned by the GL thread.
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext eglContext_ = EGL_NO_CONTEXT;
  EGLSurface surface_ = EGL_NO_SURFACE;
  EGLConfig config_ = nullptr;
  EGLint eglClientVersion_ = 3;
};

// ---------------------------------------------------------------------------
// Host registry & JSI plumbing (shared between the TurboModule and the
// XComponent callbacks living in the same .so).
// ---------------------------------------------------------------------------

// [JS thread] Remember the JSI runtime + call invoker so the XComponent
// surface callbacks can prepare contexts. Called from `prepareGLRuntime`.
void HostSetJsiRuntime(
  void *runtime,
  std::shared_ptr<facebook::react::CallInvoker> invoker);

void *HostGetJsiRuntime();
std::shared_ptr<facebook::react::CallInvoker> HostGetCallInvoker();

// [Any thread] Cache dir used for snapshot files. Set from ArkTS (which has
// the UIAbilityContext) via the NAPI export; falls back to the app's
// el2 sandbox cache path.
void HostSetCacheDir(const std::string &dir);
std::string HostGetCacheDir();

// [UI thread, XComponent callback] Surface lifecycle.
void HostOnSurfaceCreated(const std::string &xcomponentId, void *window);
void HostOnSurfaceChanged(
  const std::string &xcomponentId,
  uint32_t width,
  uint32_t height);
void HostOnSurfaceDestroyed(const std::string &xcomponentId);

// XComponents whose surfaces were created with a zero size; keyed by
// XComponent id. Mirrors Android's `onSurfaceTextureWasCalledWithZeroSize`
// wait logic. `mutex` guards `windows`.
struct PendingSurfaceWindows {
  std::mutex mutex;
  std::unordered_map<std::string, void *> windows;
};
PendingSurfaceWindows &GetPendingSurfaceWindows();

// Look up a host by context id (surface-bound or headless).
std::shared_ptr<GLHostContext> HostFindContextById(EXGLContextId ctxId);

// [JS thread, TurboModule] Register a headless context so snapshots and
// destroyContextAsync can reach it later.
void HostRegisterHeadlessContext(std::shared_ptr<GLHostContext> host);

// [JS thread, TurboModule] Drop a host from all registries (before teardown).
void HostReleaseContext(std::shared_ptr<GLHostContext> host);

// [JS thread, TurboModule] Destroy a registered GL object (upstream: camera
// textures). Returns false when the id is unknown — same as upstream.
bool HostDestroyObject(EXGLObjectId objId);

// [JS thread, from the TurboModule] Prepare a surface-bound context:
// install WebGL bindings and inject the context into the JS runtime, then
// notify ArkTS (which emits `onSurfaceCreate`). Hosts created before the JS
// runtime was ready are kept in a pending list and drained by
// `prepareGLRuntime`.
void HostPrepareSurfaceContext(std::shared_ptr<GLHostContext> host);

// [JS thread, from the TurboModule] Prepare any surface contexts created
// before the JS runtime was registered.
void DrainPendingSurfacePrepares();

// [JS thread] ArkTS notification hook (implemented in ExpoGLNapi.cpp via a
// napi_threadsafe_function).
void HostNotifySurfaceReady(const std::string &xcomponentId, EXGLContextId ctxId);

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

struct SnapshotParams {
  bool flip = false;
  bool hasFramebuffer = false;
  EXGLObjectId framebufferId = 0;
  bool hasRect = false;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  std::string format = "jpeg";
  double compress = 1.0;
};

struct SnapshotResult {
  bool ok = false;
  std::string uri;
  int width = 0;
  int height = 0;
  std::string errorCode;
  std::string errorMessage;
};

// Reads the framebuffer on the host's GL thread, encodes it with the Image
// Kit C API on a background thread and invokes `callback` with a `file://`
// URI inside the app sandbox cache dir.
void HostTakeSnapshot(
  std::shared_ptr<GLHostContext> host,
  SnapshotParams params,
  std::function<void(SnapshotResult)> callback);

} // namespace gl_cpp
} // namespace expo
