#include "ExpoGLModule.h"

#include <ReactCommon/TurboModuleUtils.h>

#include <memory>
#include <string>

#include "ExpoGLHost.h"
#include "exgl/EXGLNativeApi.h"
#include "exgl/EXPlatformUtils.h"

namespace rnoh {

using namespace facebook;

namespace {

jsi::Value createRejectPromise(jsi::Runtime &rt, const std::string &message) {
  return react::createPromiseAsJSIValue(
    rt, [message](jsi::Runtime &rt, std::shared_ptr<react::Promise> promise) {
      promise->reject(message);
    });
}

// Parses the upstream SnapshotOptions object
// (flip / framebuffer.id / rect / format / compress).
expo::gl_cpp::SnapshotParams parseSnapshotParams(
  jsi::Runtime &rt,
  const jsi::Value &options) {
  expo::gl_cpp::SnapshotParams params;
  if (!options.isObject()) {
    return params;
  }
  auto opts = options.getObject(rt);

  if (opts.hasProperty(rt, "flip")) {
    auto flip = opts.getProperty(rt, "flip");
    if (flip.isBool()) {
      params.flip = flip.getBool();
    }
  }
  if (opts.hasProperty(rt, "framebuffer")) {
    auto framebuffer = opts.getProperty(rt, "framebuffer");
    if (framebuffer.isObject()) {
      auto fb = framebuffer.getObject(rt);
      if (fb.hasProperty(rt, "id")) {
        auto id = fb.getProperty(rt, "id");
        if (id.isNumber()) {
          params.hasFramebuffer = true;
          params.framebufferId = static_cast<EXGLObjectId>(id.getNumber());
        }
      }
    }
  }
  if (opts.hasProperty(rt, "rect")) {
    auto rectValue = opts.getProperty(rt, "rect");
    if (rectValue.isObject()) {
      auto rect = rectValue.getObject(rt);
      auto readNumber = [&](const char *key, double fallback) -> double {
        if (!rect.hasProperty(rt, key)) {
          return fallback;
        }
        auto value = rect.getProperty(rt, key);
        return value.isNumber() ? value.getNumber() : fallback;
      };
      params.hasRect = true;
      params.x = static_cast<int>(readNumber("x", 0));
      params.y = static_cast<int>(readNumber("y", 0));
      params.width = static_cast<int>(readNumber("width", 0));
      params.height = static_cast<int>(readNumber("height", 0));
    }
  }
  if (opts.hasProperty(rt, "format")) {
    auto format = opts.getProperty(rt, "format");
    if (format.isString()) {
      params.format = format.getString(rt).utf8(rt);
    }
  }
  if (opts.hasProperty(rt, "compress")) {
    auto compress = opts.getProperty(rt, "compress");
    if (compress.isNumber()) {
      params.compress = compress.getNumber();
    }
  }
  return params;
}

} // namespace

ExpoGLImpl::ExpoGLImpl(const TurboModule::Context ctx, const std::string name)
    : Super(ctx, name), m_ctx(ctx) {
  // Replace the generated ArkTS-bridging method table with direct C++
  // implementations (the "Cxx TurboModule" pattern from the RNOH docs).
  methodMap_ = {
    {"prepareGLRuntime",
     {0,
      [](jsi::Runtime &rt, react::TurboModule &tm, const jsi::Value *, size_t) -> jsi::Value {
        static_cast<ExpoGLImpl &>(tm).prepareGLRuntime(rt);
        return nullptr;
      }}},
    {"createContextAsync",
     {0,
      [](jsi::Runtime &rt, react::TurboModule &tm, const jsi::Value *, size_t) -> jsi::Value {
        return static_cast<ExpoGLImpl &>(tm).createContextAsync(rt);
      }}},
    {"destroyContextAsync",
     {1,
      [](jsi::Runtime &rt, react::TurboModule &tm, const jsi::Value *args, size_t) -> jsi::Value {
        return static_cast<ExpoGLImpl &>(tm).destroyContextAsync(rt, args[0].asNumber());
      }}},
    {"takeSnapshotAsync",
     {2,
      [](jsi::Runtime &rt, react::TurboModule &tm, const jsi::Value *args, size_t) -> jsi::Value {
        return static_cast<ExpoGLImpl &>(tm).takeSnapshotAsync(rt, args[0].asNumber(), args[1]);
      }}},
    {"destroyObjectAsync",
     {1,
      [](jsi::Runtime &rt, react::TurboModule &tm, const jsi::Value *args, size_t) -> jsi::Value {
        return static_cast<ExpoGLImpl &>(tm).destroyObjectAsync(rt, args[0].asNumber());
      }}},
  };
}

void ExpoGLImpl::prepareGLRuntime(jsi::Runtime &rt) {
  expo::gl_cpp::HostSetJsiRuntime(&rt, m_ctx.jsInvoker);
  // Installs the WebGL constructors/constants on the global object; safe and
  // idempotent to call repeatedly (guarded internally).
  EXGLInstallWebGLBindings(&rt);
  expo::gl_cpp::DrainPendingSurfacePrepares();
}

jsi::Value ExpoGLImpl::createContextAsync(jsi::Runtime &rt) {
  prepareGLRuntime(rt);

  // Headless context: same as Android calling GLContext.initialize(null, ...).
  auto host = std::make_shared<expo::gl_cpp::GLHostContext>("", nullptr);
  host->start();
  host->exglCtxId = EXGLContextCreate();
  expo::gl_cpp::HostRegisterHeadlessContext(host);

  auto flushMethod = [host] { host->enqueue([] {}); };
  EXGLContextPrepare(&rt, host->exglCtxId, flushMethod);

  return react::createPromiseAsJSIValue(
    rt, [host](jsi::Runtime &rt, std::shared_ptr<react::Promise> promise) {
      auto result = jsi::Object(rt);
      result.setProperty(rt, "exglCtxId", static_cast<double>(host->exglCtxId));
      promise->resolve(jsi::Value(rt, result));
    });
}

jsi::Value ExpoGLImpl::destroyContextAsync(jsi::Runtime & /*rt*/, double exglCtxId) {
  auto ctxId = static_cast<EXGLContextId>(exglCtxId);
  auto host = expo::gl_cpp::HostFindContextById(ctxId);
  if (host == nullptr) {
    return jsi::Value(false);
  }
  expo::gl_cpp::HostReleaseContext(host);
  host->stopAndJoin();
  EXGLContextDestroy(ctxId);
  return jsi::Value(true);
}

jsi::Value ExpoGLImpl::takeSnapshotAsync(
  jsi::Runtime &rt,
  double exglCtxId,
  const jsi::Value &options) {
  auto ctxId = static_cast<EXGLContextId>(exglCtxId);
  auto host = expo::gl_cpp::HostFindContextById(ctxId);
  if (host == nullptr) {
    return createRejectPromise(rt, "GLContext not found for given context id");
  }

  auto params = parseSnapshotParams(rt, options);
  auto invoker = m_ctx.jsInvoker;
  // The runtime outlives this call (RN instance lifetime); only the reference
  // to it is rebound, so capturing the pointer for the async resolution is
  // safe. The snapshot resolution must build jsi values on the JS thread.
  auto *runtimePtr = &rt;

  return react::createPromiseAsJSIValue(
    rt,
    [host, params, invoker, runtimePtr](jsi::Runtime &,
                                        std::shared_ptr<react::Promise> promise) {
      expo::gl_cpp::HostTakeSnapshot(
        host,
        params,
        [invoker, promise, runtimePtr](expo::gl_cpp::SnapshotResult result) {
          invoker->invokeAsync([promise, result, runtimePtr] {
            if (!result.ok) {
              promise->reject(result.errorCode + ": " + result.errorMessage);
              return;
            }
            jsi::Runtime &rt = *runtimePtr;
            auto snapshot = jsi::Object(rt);
            snapshot.setProperty(rt, "uri", result.uri);
            snapshot.setProperty(rt, "localUri", result.uri);
            snapshot.setProperty(rt, "width", static_cast<double>(result.width));
            snapshot.setProperty(rt, "height", static_cast<double>(result.height));
            promise->resolve(jsi::Value(rt, snapshot));
          });
        });
    });
}

jsi::Value ExpoGLImpl::destroyObjectAsync(jsi::Runtime & /*rt*/, double exglObjId) {
  // GL objects are registered by producers (upstream: camera textures via
  // createCameraTextureAsync). Camera textures are not available on HarmonyOS
  // in the first phase, so — mirroring upstream behavior for unknown ids —
  // destroying an unknown object resolves to `false`.
  return jsi::Value(expo::gl_cpp::HostDestroyObject(
    static_cast<EXGLObjectId>(exglObjId)));
}

} // namespace rnoh
