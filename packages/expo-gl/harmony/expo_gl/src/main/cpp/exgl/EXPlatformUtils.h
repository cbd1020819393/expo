#pragma once

#ifdef __APPLE__

namespace expo {
namespace gl_cpp {
void EXiOSLog(const char *msg, ...) __attribute__((format(printf, 1, 2)));

typedef struct {
  long majorVersion;
  long minorVersion;
  long patchVersion;
} EXiOSOperatingSystemVersion;

EXiOSOperatingSystemVersion EXiOSGetOperatingSystemVersion(void);
} // namespace gl_cpp
} // namespace expo

#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

#ifdef __OHOS__
#include <hilog/log.h>
#endif

#define EXGL_DEBUG // Whether debugging is on

#ifdef EXGL_DEBUG
#ifdef __ANDROID__
#define EXGLSysLog(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, "EXGL", fmt, ##__VA_ARGS__)
#endif
#ifdef __OHOS__
// hilog's format attribute and %s-style formatting work as expected; the
// OHOS variant routes to the same "EXGL" domain tag as the other platforms.
// NOTE: the hilog domain must fit in 16 bits — a value like 0xE0101 gets the
// log silently dropped.
#define EXGLSysLog(fmt, ...) \
  OH_LOG_Print(LOG_APP, LOG_ERROR, 0xC0DE, "EXGL", fmt, ##__VA_ARGS__)
#endif
#ifdef __APPLE__
#define EXGLSysLog(fmt, ...) EXiOSLog("EXGL: " fmt, ##__VA_ARGS__)
#endif
#else
#define EXGLSysLog(...)
#endif
