/**
 * Hand-written counterpart of a "react-native codegen-harmony" artifact,
 * following the exact shape of generated/ExpoGL.cpp. See ExpoAsset.h.
 */
#include "ExpoAsset.h"

namespace rnoh {
using namespace facebook;

ExpoAsset::ExpoAsset(const ArkTSTurboModule::Context ctx, const std::string name) : ArkTSTurboModule(ctx, name) {
    methodMap_ = {
        // Contract (generated/ts/turboModules/ExpoAsset.ts):
        // downloadAsync(uri: string, md5: string | null, type: string | null)
        //   => Promise<{ uri: string, width?: number, height?: number }>
        ARK_ASYNC_METHOD_METADATA(downloadAsync, 3),
    };
}

} // namespace rnoh
