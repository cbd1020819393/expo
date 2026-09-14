#pragma once

#include "pch.h"

#include "EXGLNativeContext.h"
#include "EXGLImageUtils.h"
#include "EXWebGLRenderer.h"

#include <string>

namespace expo {
namespace gl_cpp {

// [B-29] Shared guard for the pixel upload/read entry points (readPixels,
// texImage2D/3D, texSubImage2D/3D): computes the expected byte size in 64-bit
// (JS-supplied dimensions can wrap 32-bit math) and throws a JS-visible error
// when the source buffer cannot hold `width * height * depth` texels.
inline void validatePixelBufferSize(
  facebook::jsi::Runtime &runtime,
  const char *apiName,
  GLsizei width,
  GLsizei height,
  GLsizei depth,
  GLenum type,
  GLenum format,
  size_t availableBytes) {
  if (width < 0 || height < 0 || depth < 0) {
    throw jsi::JSError(
      runtime, std::string("EXGL: gl.") + apiName + ": width, height and depth must be >= 0");
  }
  size_t expectedBytes = static_cast<size_t>(width) * static_cast<size_t>(height)
    * static_cast<size_t>(depth) * static_cast<size_t>(bytesPerPixel(type, format));
  if (availableBytes < expectedBytes) {
    throw jsi::JSError(
      runtime,
      std::string("EXGL: gl.") + apiName + ": buffer is too small - expected at least "
        + std::to_string(expectedBytes) + " bytes for a " + std::to_string(width) + "x"
        + std::to_string(height) + "x" + std::to_string(depth) + " pixel upload, got "
        + std::to_string(availableBytes));
  }
}

template<typename Func>
inline jsi::Value exglGetActiveInfo(
  EXGLContext *ctx,
  jsi::Runtime &runtime,
  EXGLObjectId fProgram,
  GLuint index,
  GLenum lengthParam,
  Func glFunc) {
  if (fProgram == 0) {
    return nullptr;
  }

  // [B-9] Zero-initialize every GL query output: an invalid program handle
  // makes glGetProgramiv skip the write, and the previous uninitialized
  // `maxNameLength` fed name.resize() with garbage.
  GLsizei length = 0;
  GLint size = 0;
  GLenum type = 0;
  std::string name;
  GLint maxNameLength = 0;

  ctx->addBlockingToNextBatch([&] {
    GLuint program = ctx->lookupObject(fProgram);
    // [B-9] Only query live programs; otherwise fall through to the empty-name
    // (null) path below.
    if (program == 0 || !glIsProgram(program)) {
      return;
    }
    glGetProgramiv(program, lengthParam, &maxNameLength);
    name.resize(maxNameLength);

    glFunc(program, index, maxNameLength, &length, &size, &type, &name[0]);
    name.resize(length);
  });

  if (name.size() == 0) { // name.length() may be larger
    return nullptr;
  }

  jsi::Object jsResult =
    createWebGLObject(runtime, EXWebGLClass::WebGLActiveInfo, {}).asObject(runtime);
  jsResult.setProperty(runtime, "name", jsi::String::createFromUtf8(runtime, name));
  jsResult.setProperty(runtime, "size", size);
  jsResult.setProperty(runtime, "type", static_cast<double>(type));
  return jsResult;
}

template<typename Func, typename... T>
inline jsi::Value exglCall(EXGLContext *ctx, Func func, T &&... args) {
  ctx->addToNextBatch([=, args = std::make_tuple(std::forward<T>(args)...)] {
    return std::apply(func, std::move(args));
  });
}

template<typename Func, typename T>
inline jsi::Value
exglUniformv(EXGLContext *ctx, Func func, GLuint uniform, size_t dim, std::vector<T> &&data) {
  ctx->addToNextBatch([=, data{std::move(data)}] {
    func(uniform, static_cast<int>(data.size() / dim), data.data());
  });
  return nullptr;
}

template<typename Func, typename T>
inline jsi::Value exglUniformMatrixv(
  EXGLContext *ctx,
  Func func,
  GLuint uniform,
  GLboolean transpose,
  size_t dim,
  std::vector<T> &&data) {
  ctx->addToNextBatch([=, data{std::move(data)}] {
    func(uniform, static_cast<int>(data.size() / dim), transpose, data.data());
  });
  return nullptr;
}

template<typename Func, typename T>
inline jsi::Value
exglVertexAttribv(EXGLContext *ctx, Func func, GLuint index, std::vector<T> &&data) {
  ctx->addToNextBatch([=, data{std::move(data)}] { func(index, data.data()); });
  return nullptr;
}

inline jsi::Value
exglIsObject(EXGLContext *ctx, EXGLObjectId id, std::function<GLboolean(GLuint)> func) {
  GLboolean glResult;
  ctx->addBlockingToNextBatch([&] { glResult = func(ctx->lookupObject(id)); });
  return glResult == GL_TRUE;
}

inline jsi::Value exglGenObject(
  EXGLContext *ctx,
  jsi::Runtime &runtime,
  std::function<void(GLsizei, EXGLObjectId *)> func,
  EXWebGLClass webglClass) {
  auto id = ctx->addFutureToNextBatch(runtime, [=] {
    GLuint buffer;
    func(1, &buffer);
    return buffer;
  });
  return createWebGLObject(runtime, webglClass, {std::move(id)});
}

inline jsi::Value exglCreateObject(
  EXGLContext *ctx,
  jsi::Runtime &runtime,
  std::function<GLuint()> func,
  EXWebGLClass webglClass) {
  auto id = ctx->addFutureToNextBatch(runtime, [=] { return func(); });
  return createWebGLObject(runtime, webglClass, {std::move(id)});
}

inline jsi::Value
exglDeleteObject(EXGLContext *ctx, EXGLObjectId id, std::function<void(EXGLObjectId)> func) {
  ctx->addToNextBatch([=] { func(ctx->lookupObject(id)); });
  return nullptr;
}

inline jsi::Value exglDeleteObject(
  EXGLContext *ctx,
  EXGLObjectId id,
  std::function<void(GLsizei, const EXGLObjectId *)> func) {
  ctx->addToNextBatch([=] {
    GLuint buffer = ctx->lookupObject(id);
    func(1, &buffer);
  });
  return nullptr;
}

inline jsi::Value exglUnimplemented(std::string name) {
  throw std::runtime_error("EXGL: " + name + "() isn't implemented yet!");
}

} // namespace gl_cpp
} // namespace expo
