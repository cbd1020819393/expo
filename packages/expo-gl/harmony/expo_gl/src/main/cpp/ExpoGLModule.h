#pragma once

#include <string>

#include "RNOH/TurboModule.h"

namespace rnoh {

// HarmonyOS (RNOH) implementation of the `ExpoGL` TurboModule.
//
// NOTE: this deliberately derives from rnoh::TurboModule instead of the
// codegen-generated `ExpoGL` (ArkTSTurboModule) class. RNOH's
// TurboModuleFactory::create() rejects any delegate-created module that
// dynamic_pointer_casts to ArkTSTurboModule unless an ArkTS-side instance
// exists ("Couldn't find Turbo Module ... on the ArkTS side"), which is
// incompatible with this pure C++ implementation. The GL core needs raw
// `jsi::Runtime` access (it installs the WebGL bindings onto the JS global
// object) which only a native C++ TurboModule call provides — the same
// reason upstream goes through JNI on Android (EXGLJniApi.cpp).
class ExpoGLImpl : public TurboModule {
 public:
  using Super = TurboModule;

  ExpoGLImpl(const TurboModule::Context ctx, const std::string name);

  // Idempotent. Registers the JSI runtime + call invoker for surface
  // preparation, installs the WebGL global bindings and drains any surface
  // contexts created before the runtime was ready.
  void prepareGLRuntime(facebook::jsi::Runtime &rt);

  facebook::jsi::Value createContextAsync(facebook::jsi::Runtime &rt);

  facebook::jsi::Value destroyContextAsync(
      facebook::jsi::Runtime &rt, double exglCtxId);

  facebook::jsi::Value takeSnapshotAsync(
      facebook::jsi::Runtime &rt,
      double exglCtxId,
      const facebook::jsi::Value &options);

  facebook::jsi::Value destroyObjectAsync(
      facebook::jsi::Runtime &rt, double exglObjId);

 protected:
  // rnoh::TurboModule (unlike ArkTSTurboModule) does not keep the context;
  // hold it ourselves — the GL host reads jsInvoker from it.
  TurboModule::Context m_ctx;
};

} // namespace rnoh
