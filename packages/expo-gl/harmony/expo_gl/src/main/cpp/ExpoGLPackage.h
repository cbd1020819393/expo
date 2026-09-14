#ifndef EXPOGLPACKAGE_H
#define EXPOGLPACKAGE_H

#pragma once

#include <memory>

#include <hilog/log.h>
#include "generated/RNOHGeneratedPackage.h"
#include "ExpoGLModule.h"

#define EXPOGL_PROBE(fmt, ...) \
  OH_LOG_Print(LOG_APP, LOG_INFO, 0xC0DE, "ExpoGLProbe", fmt, ##__VA_ARGS__)

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

    ExpoGlPackage(Package::Context ctx) : Super(ctx) {
      EXPOGL_PROBE("package constructed");
    }

    std::unique_ptr<TurboModuleFactoryDelegate> createTurboModuleFactoryDelegate() override {
      EXPOGL_PROBE("createTurboModuleFactoryDelegate called");
      return std::make_unique<ExpoGlTurboModuleFactoryDelegate>();
    }

  private:
    class ExpoGlTurboModuleFactoryDelegate : public TurboModuleFactoryDelegate {
      public:
        SharedTurboModule createTurboModule(Context ctx, const std::string &name) const override {
          EXPOGL_PROBE("createTurboModule name=%{public}s", name.c_str());
          if (name == "ExpoGL") {
            try {
              auto m = std::make_shared<ExpoGLImpl>(ctx, name);
              EXPOGL_PROBE("ExpoGLImpl constructed OK, ptr=%{public}p", (void *)m.get());
              return m;
            } catch (const std::exception &e) {
              EXPOGL_PROBE("ExpoGLImpl ctor THREW std::exception: %{public}s", e.what());
            } catch (...) {
              EXPOGL_PROBE("ExpoGLImpl ctor THREW non-std exception");
            }
          }
          return nullptr;
        }
    };
};
} // namespace rnoh
#endif //EXPOGLPACKAGE_H
