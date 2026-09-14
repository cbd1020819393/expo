#pragma once

#include <string>

#include "exgl/EXGLNativeApi.h"

namespace expo {
namespace gl_cpp {

// Implemented in ExpoGLNapi.cpp. Lets the rest of the native code notify the
// ArkTS Fabric component that a surface-bound GL context is ready.
void NapiNotifySurfaceReady(const std::string &xcomponentId, EXGLContextId ctxId);

} // namespace gl_cpp
} // namespace expo
