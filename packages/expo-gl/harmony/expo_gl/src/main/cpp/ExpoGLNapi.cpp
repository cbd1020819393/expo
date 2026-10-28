#include "ExpoGLNapi.h"

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <native_window/external_window.h>
#include <napi/native_api.h>

#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "ExpoGLHost.h"
#include "exgl/EXGLNativeApi.h"
#include "exgl/EXPlatformUtils.h"

namespace expo {
namespace gl_cpp {

// ---------------------------------------------------------------------------
// Surface-ready notification: C++ -> ArkTS via a napi_threadsafe_function.
// The ArkTS ExpoGLView component registers one listener; it routes by
// XComponent id.
// ---------------------------------------------------------------------------

namespace {

struct SurfaceReadyEvent {
  std::string xcomponentId;
  EXGLContextId ctxId;
};

napi_threadsafe_function g_surfaceTsfn = nullptr;
std::mutex g_tsfnMutex;

void CallSurfaceListener(
  napi_env env,
  napi_value jsCallback,
  void * /*context*/,
  void *data) {
  auto *event = static_cast<SurfaceReadyEvent *>(data);
  do {
    if (env == nullptr || jsCallback == nullptr) {
      break;
    }
    napi_value args[2];
    napi_create_string_utf8(
      env, event->xcomponentId.c_str(), event->xcomponentId.size(), &args[0]);
    napi_create_uint32(env, static_cast<uint32_t>(event->ctxId), &args[1]);
    napi_value undefined;
    napi_get_undefined(env, &undefined);
    napi_call_function(env, undefined, jsCallback, 2, args, nullptr);
  } while (false);
  delete event;
}

} // namespace

void NapiNotifySurfaceReady(const std::string &xcomponentId, EXGLContextId ctxId) {
  napi_threadsafe_function tsfn = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_tsfnMutex);
    tsfn = g_surfaceTsfn;
  }
  if (tsfn == nullptr) {
    // [A-7] Make the dropped notification diagnosable: the ArkTS listener
    // registers in aboutToAppear, so a prepare that finished earlier had
    // nowhere to deliver — previously this was a silent loss.
    EXGLSysLog(
      "EXGL: surface-ready notification for %s dropped - no JS listener registered yet",
      xcomponentId.c_str());
    return;
  }
  auto *event = new SurfaceReadyEvent{xcomponentId, ctxId};
  if (napi_call_threadsafe_function(tsfn, event, napi_tsfn_nonblocking) != napi_ok) {
    delete event;
  }
}

// ---------------------------------------------------------------------------
// NAPI exports (also imported directly from `libexpo_gl.so` by the ArkTS
// component for cache-dir registration and the listener).
// ---------------------------------------------------------------------------

namespace {

napi_value SetCacheDirExport(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc >= 1) {
    size_t len = 0;
    if (napi_get_value_string_utf8(env, argv[0], nullptr, 0, &len) == napi_ok && len > 0) {
      std::string dir(len, '\0');
      if (napi_get_value_string_utf8(env, argv[0], dir.data(), len + 1, &len) == napi_ok) {
        // Strip a trailing file:// prefix if a caller passed a URI.
        if (dir.rfind("file://", 0) == 0) {
          dir = dir.substr(7);
        }
        HostSetCacheDir(dir);
      }
    }
  }
  napi_value undefined;
  napi_get_undefined(env, &undefined);
  return undefined;
}

napi_value RegisterSurfaceListenerExport(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc >= 1) {
    std::lock_guard<std::mutex> lock(g_tsfnMutex);
    if (g_surfaceTsfn == nullptr) {
      napi_value resourceName;
      napi_create_string_utf8(env, "expo-gl-surface-listener", NAPI_AUTO_LENGTH, &resourceName);
      if (napi_create_threadsafe_function(
            env,
            argv[0],
            nullptr,
            resourceName,
            0,
            1,
            nullptr,
            nullptr,
            nullptr,
            CallSurfaceListener,
            &g_surfaceTsfn) != napi_ok) {
        g_surfaceTsfn = nullptr;
        EXGLSysLog("EXGL: failed to create surface listener threadsafe function");
      }
    }
  }
  napi_value undefined;
  napi_get_undefined(env, &undefined);
  return undefined;
}

// Modern surface handoff (no XComponent "library" handshake): the ArkTS view
// renders an XComponent with an XComponentController, obtains its surfaceId
// in onLoad and hands it to us here. We create the OHNativeWindow from the
// surfaceId and run the exact same flow as OnSurfaceCreatedCb. This avoids
// the nm_register_func race where the ArkTS `import 'libexpo_gl.so'` loads
// the module before any XComponent exists, permanently losing the
// OH_NativeXComponent handshake.
napi_value PrepareSurfaceExport(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value argv[4] = {nullptr, nullptr, nullptr, nullptr};
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc < 2) {
    EXGLSysLog("EXGL: prepareSurface requires (surfaceId, xcomponentId, [w, h])");
    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
  }

  size_t len = 0;
  char surfaceIdStr[64] = {0};
  if (napi_get_value_string_utf8(env, argv[0], surfaceIdStr, sizeof(surfaceIdStr), &len) != napi_ok) {
    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
  }
  uint64_t surfaceId = strtoull(surfaceIdStr, nullptr, 10);

  // Optional pixel size observed on the ArkTS side via
  // XComponentController.getXComponentSize(). Producer windows created from a
  // surfaceId never receive a buffer geometry from the framework, so without
  // an explicit size EGL cannot allocate swapchain buffers (EGL_BAD_ALLOC).
  double width = 0;
  double height = 0;
  if (argc >= 4) {
    napi_get_value_double(env, argv[2], &width);
    napi_get_value_double(env, argv[3], &height);
  }

  OHNativeWindow *window = nullptr;
  int32_t rc = OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfaceId, &window);
  if (rc != 0 || window == nullptr) {
    EXGLSysLog("EXGL: CreateNativeWindowFromSurfaceId(%s) failed rc=%{public}d", surfaceIdStr, rc);
    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
  }

  char id[256] = {0};
  if (napi_get_value_string_utf8(env, argv[1], id, sizeof(id), &len) != napi_ok) {
    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
  }

  auto idStr = std::string(id);
  auto windowShared = window;
  std::thread([windowShared, idStr, surfaceIdStr, width, height]() {
    int32_t geoWidth = static_cast<int32_t>(width);
    int32_t geoHeight = static_cast<int32_t>(height);
    if (geoWidth > 0 && geoHeight > 0) {
      // SET_BUFFER_GEOMETRY takes (width, height); GET takes (height, width) —
      // the asymmetric order is documented in external_window.h.
      OH_NativeWindow_NativeWindowHandleOpt(windowShared, SET_BUFFER_GEOMETRY,
          geoWidth, geoHeight);
      EXGLSysLog("EXGL: surface %s sized %dx%d from ArkTS", idStr.c_str(),
          geoWidth, geoHeight);
    } else {
      // Legacy path: ArkTS did not hand over a size. Give the framework a
      // brief chance to size the window, then fall back to a fixed geometry
      // so the pipeline can still be validated end-to-end.
      for (int attempt = 0; attempt < 60; ++attempt) {
        int32_t pollHeight = 0;
        int32_t pollWidth = 0;
        int32_t qrc = OH_NativeWindow_NativeWindowHandleOpt(windowShared,
            GET_BUFFER_GEOMETRY, &pollHeight, &pollWidth);
        if (qrc == 0 && pollWidth > 0 && pollHeight > 0) {
          geoWidth = pollWidth;
          geoHeight = pollHeight;
          EXGLSysLog("EXGL: surface %s sized %dx%d after %d polls", idStr.c_str(),
              geoWidth, geoHeight, attempt);
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (geoWidth <= 0 || geoHeight <= 0) {
        geoWidth = 875;
        geoHeight = 875;
        OH_NativeWindow_NativeWindowHandleOpt(windowShared, SET_BUFFER_GEOMETRY,
            geoWidth, geoHeight);
        EXGLSysLog("EXGL: forced fallback geometry 875x875");
      }
    }
    EXGLSysLog("EXGL: attaching surface %s with geometry %dx%d", idStr.c_str(),
        geoWidth, geoHeight);
    HostOnSurfaceCreated(idStr, windowShared);
  }).detach();

  napi_value undefined;
  napi_get_undefined(env, &undefined);
  return undefined;
}

// NAPI `destroySurface`, called from the ArkTS view's aboutToDisappear: the
// controller-based XComponent never delivers OnSurfaceDestroyedCb, so this is
// the only teardown path for surfaceId-backed hosts.
napi_value DestroySurfaceExport(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1] = {nullptr};
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argc >= 1) {
    size_t len = 0;
    if (napi_get_value_string_utf8(env, argv[0], nullptr, 0, &len) == napi_ok && len > 0) {
      std::string id(len, '\0');
      if (napi_get_value_string_utf8(env, argv[0], id.data(), len + 1, &len) == napi_ok) {
        HostDestroySurfaceContext(id);
      }
    }
  }
  napi_value undefined;
  napi_get_undefined(env, &undefined);
  return undefined;
}

// ---------------------------------------------------------------------------
// XComponent surface callbacks (UI thread)
// ---------------------------------------------------------------------------

// Android's GLView waits for a non-zero surface size before initializing
// (onSurfaceTextureAvailable can fire with a 0-sized texture); mirror that
// with a pending map drained on the first OnSurfaceChanged with a real size.
constexpr uint32_t kMaxXComponentIdLength = 256;

std::string GetXComponentId(OH_NativeXComponent *component) {
  char id[kMaxXComponentIdLength];
  uint64_t size = kMaxXComponentIdLength;
  if (component != nullptr && OH_NativeXComponent_GetXComponentId(component, id, &size) ==
    OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
    return std::string(id);
  }
  return {};
}

void OnSurfaceCreatedCb(OH_NativeXComponent *component, void *window) {
  std::string id = GetXComponentId(component);
  if (id.empty() || window == nullptr) {
    EXGLSysLog("EXGL: OnSurfaceCreated with invalid component or window");
    return;
  }

  // Defer creation when the surface has no size yet (mirrors Android).
  uint64_t width = 0;
  uint64_t height = 0;
  if (OH_NativeXComponent_GetXComponentSize(component, window, &width, &height) ==
        OH_NATIVEXCOMPONENT_RESULT_SUCCESS &&
      (width == 0 || height == 0)) {
    auto &pending = GetPendingSurfaceWindows();
    std::lock_guard<std::mutex> lock(pending.mutex);
    pending.windows[id] = window;
    return;
  }
  HostOnSurfaceCreated(id, window);
}

void OnSurfaceChangedCb(OH_NativeXComponent *component, void *window) {
  std::string id = GetXComponentId(component);
  if (id.empty() || window == nullptr) {
    return;
  }

  {
    auto &pending = GetPendingSurfaceWindows();
    std::lock_guard<std::mutex> lock(pending.mutex);
    auto iter = pending.windows.find(id);
    if (iter != pending.windows.end()) {
      uint64_t width = 0;
      uint64_t height = 0;
      if (OH_NativeXComponent_GetXComponentSize(component, window, &width, &height) ==
            OH_NATIVEXCOMPONENT_RESULT_SUCCESS &&
          width > 0 && height > 0) {
        pending.windows.erase(iter);
        HostOnSurfaceCreated(id, window);
        return;
      }
    }
  }

  uint64_t width = 0;
  uint64_t height = 0;
  if (OH_NativeXComponent_GetXComponentSize(component, window, &width, &height) ==
      OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
    HostOnSurfaceChanged(id, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  }
}

void OnSurfaceDestroyedCb(OH_NativeXComponent *component, void * /*window*/) {
  std::string id = GetXComponentId(component);
  if (id.empty()) {
    return;
  }
  HostOnSurfaceDestroyed(id);
}

OH_NativeXComponent_Callback g_xcomponentCallback = {
  OnSurfaceCreatedCb,
  OnSurfaceChangedCb,
  OnSurfaceDestroyedCb,
  nullptr,
};

napi_value InitExpoGlModule(napi_env env, napi_value exports) {
  EXGLSysLog("EXGL: InitExpoGlModule enter");
  napi_property_descriptor desc[] = {
    {"setCacheDir", nullptr, SetCacheDirExport, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"registerSurfaceListener", nullptr, RegisterSurfaceListenerExport, nullptr, nullptr, nullptr,
     napi_default, nullptr},
    {"prepareSurface", nullptr, PrepareSurfaceExport, nullptr, nullptr, nullptr, napi_default,
     nullptr},
    {"destroySurface", nullptr, DestroySurfaceExport, nullptr, nullptr, nullptr, napi_default,
     nullptr},
  };
  napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

  // XComponent (type "surface", library "expo_gl") hands us the native
  // component through this well-known export property.
  napi_value exportInstance = nullptr;
  napi_status propStatus =
      napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance);
  EXGLSysLog("EXGL: XComponent property status=%{public}d", (int)propStatus);
  if (propStatus == napi_ok && exportInstance != nullptr) {
    OH_NativeXComponent *nativeXComponent = nullptr;
    napi_status unwrapStatus =
        napi_unwrap(env, exportInstance, reinterpret_cast<void **>(&nativeXComponent));
    EXGLSysLog("EXGL: XComponent unwrap status=%{public}d ptr=%{public}p", (int)unwrapStatus,
        (void *)nativeXComponent);
    if (unwrapStatus == napi_ok && nativeXComponent != nullptr) {
      if (OH_NativeXComponent_RegisterCallback(nativeXComponent, &g_xcomponentCallback) ==
          OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        EXGLSysLog("EXGL: XComponent callbacks registered");
      } else {
        EXGLSysLog("EXGL: OH_NativeXComponent_RegisterCallback failed");
      }
    }
  }
  return exports;
}

} // namespace

} // namespace gl_cpp
} // namespace expo

using namespace expo::gl_cpp;

static napi_module g_expoGlModule;

extern "C" __attribute__((constructor)) void ExpoGlRegisterNapiModule(void) {
  std::memset(&g_expoGlModule, 0, sizeof(g_expoGlModule));
  g_expoGlModule.nm_version = 1;
  g_expoGlModule.nm_flags = 0;
  g_expoGlModule.nm_filename = nullptr;
  g_expoGlModule.nm_register_func = InitExpoGlModule;
  g_expoGlModule.nm_modname = "expo_gl";
  g_expoGlModule.nm_priv = nullptr;
  napi_module_register(&g_expoGlModule);
}
