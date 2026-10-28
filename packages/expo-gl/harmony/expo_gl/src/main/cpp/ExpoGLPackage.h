#ifndef EXPOGLPACKAGE_H
#define EXPOGLPACKAGE_H

#pragma once

#include <memory>

#include "RNOH/ArkTSTurboModule.h"
#include "generated/ExpoAsset.h"
#include "generated/RNOHGeneratedPackage.h"
#include "ExpoGLModule.h"

namespace rnoh {
// Autolinking entry package for the C++ side. The generated super class
// already registers the Fabric component descriptor provider, the JSI binder
// and the event-emit request handler for ExpoGLView; this override only
// swaps the `ExpoGL` TurboModule construction from the generated
// ArkTS-bridging class to the direct C++ implementation (see ExpoGLModule.h).
class ExpoGlPackage : public RNOHGeneratedPackage {
  public:
    using Super = RNOHGeneratedPackage;
    using Super::Super;

  private:
    class ExpoGlTurboModuleFactoryDelegate : public TurboModuleFactoryDelegate {
      public:
        SharedTurboModule createTurboModule(Context ctx, const std::string &name) const override {
          if (name == "ExpoGL") {
            return std::make_shared<ExpoGLImpl>(ctx, name);
          }
          // Pure-ArkTS TurboModule (ExpoAssetModule.ets) bridged through the
          // codegen-shaped wrapper (generated/ExpoAsset.h). It fills the C++
          // method map so JS property access resolves; the actual calls are
          // forwarded to the ArkTS instance resolved by RNOH. A bare
          // ArkTSTurboModule would expose an empty method map (every method
          // undefined), and no bridge at all makes TurboModuleRegistry.get
          // return null.
          if (name == "ExpoAsset") {
            return std::make_shared<ExpoAsset>(ctx, name);
          }
          return nullptr;
        }
    };

    std::unique_ptr<TurboModuleFactoryDelegate> createTurboModuleFactoryDelegate() override {
      return std::make_unique<ExpoGlTurboModuleFactoryDelegate>();
    }
};
} // namespace rnoh
#endif //EXPOGLPACKAGE_H
