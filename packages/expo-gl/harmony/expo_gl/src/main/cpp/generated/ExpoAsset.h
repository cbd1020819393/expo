/**
 * Hand-written counterpart of a "react-native codegen-harmony" artifact,
 * following the exact shape of generated/ExpoGL.h.
 *
 * Why hand-written: the ExpoAsset TurboModule is implemented in ArkTS
 * (ExpoAssetModule.ets) and consumed by the hand-written
 * `@react-native-ohos/expo-asset` JS entry through
 * `requireNativeModule('ExpoAsset')`. RNOH resolves JS-side TurboModule
 * property access through the C++ method map below — without this class,
 * every method resolves to undefined ("native.downloadAsync is not a
 * function"). Codegen only produced artifacts for ExpoGL/ExpoGLView, so
 * this class is maintained by hand next to them.
 */
#pragma once

#include "RNOH/ArkTSTurboModule.h"

namespace rnoh {

class JSI_EXPORT ExpoAsset : public ArkTSTurboModule {
  public:
    ExpoAsset(const ArkTSTurboModule::Context ctx, const std::string name);
};

} // namespace rnoh
