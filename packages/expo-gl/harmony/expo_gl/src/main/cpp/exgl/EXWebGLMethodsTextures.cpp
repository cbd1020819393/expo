
#include "EXWebGLMethods.h"
#include "EXWebGLMethodsMacros.h"

namespace expo {
namespace gl_cpp {
namespace method {

// Textures
// --------

NATIVE_METHOD(bindTexture) {
  CTX();
  auto target = ARG(0, GLenum);
  auto texture = ARG(1, EXWebGLClass);
  ctx->addToNextBatch([=] { glBindTexture(target, ctx->lookupObject(texture)); });
  return nullptr;
}

UNIMPL_NATIVE_METHOD(compressedTexImage2D)

UNIMPL_NATIVE_METHOD(compressedTexSubImage2D)

SIMPLE_NATIVE_METHOD(
  copyTexImage2D,
  glCopyTexImage2D); // target, level, internalformat, x, y, width, height, border

SIMPLE_NATIVE_METHOD(
  copyTexSubImage2D,
  glCopyTexSubImage2D) // target, level, xoffset, yoffset, x, y, width, height

NATIVE_METHOD(createTexture) {
  CTX();
  return exglGenObject(ctx, runtime, glGenTextures, EXWebGLClass::WebGLTexture);
}

NATIVE_METHOD(deleteTexture) {
  CTX();
  return exglDeleteObject(ctx, ARG(0, EXWebGLClass), glDeleteTextures);
}

SIMPLE_NATIVE_METHOD(generateMipmap, glGenerateMipmap) // target

UNIMPL_NATIVE_METHOD(getTexParameter)

NATIVE_METHOD(isTexture) {
  CTX();
  return exglIsObject(ctx, ARG(0, EXWebGLClass), glIsTexture);
}

NATIVE_METHOD(texImage2D, 6) {
  CTX();
  auto target = ARG(0, GLenum);
  auto level = ARG(1, GLint);
  auto internalformat = ARG(2, GLint);
  if (argc == 9) {
    auto width = ARG(3, GLsizei);
    auto height = ARG(4, GLsizei);
    auto border = ARG(5, GLsizei);
    auto format = ARG(6, GLenum);
    auto type = ARG(7, GLenum);
    if (ARG(8, const jsi::Value &).isNull()) {
      ctx->addToNextBatch([=] {
        glTexImage2D(target, level, internalformat, width, height, border, format, type, nullptr);
      });
      return nullptr;
    }
    auto data = ARG(8, jsi::Object);

    if (data.isArrayBuffer(runtime) || isTypedArray(runtime, data)) {
      std::vector<uint8_t> vec = rawTypedArray(runtime, std::move(data));
      // [B-6] Validate the upload size (64-bit math) before touching the buffer.
      validatePixelBufferSize(runtime, "texImage2D", width, height, 1, type, format, vec.size());
      if (ctx->unpackFLipY) {
        flipPixels(vec.data(), width * bytesPerPixel(type, format), height);
      }
      ctx->addToNextBatch([=, vec{std::move(vec)}] {
        glTexImage2D(
          target, level, internalformat, width, height, border, format, type, vec.data());
      });
    } else {
      auto image = loadImage(runtime, data, &width, &height, nullptr);
      // [B-15] Surface the failure instead of silently uploading a black texture.
      if (image == nullptr) {
        throw jsi::JSError(
          runtime,
          "EXGL: gl.texImage2D(): failed to load the image referenced by `localUri` "
          "(only file:// URIs pointing to an existing PNG/JPEG file are supported)");
      }
      // [B-6] Decoded images are RGBA8888 — 4 bytes per pixel.
      validatePixelBufferSize(
        runtime,
        "texImage2D",
        width,
        height,
        1,
        type,
        format,
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
      if (ctx->unpackFLipY) {
        flipPixels(image.get(), width * bytesPerPixel(type, format), height);
      }
      ctx->addToNextBatch([=] {
        glTexImage2D(
          target, level, internalformat, width, height, border, format, type, image.get());
      });
    }
  } else if (argc == 6) {
    auto format = ARG(3, GLenum);
    auto type = ARG(4, GLenum);
    auto data = ARG(5, jsi::Object);
    GLsizei width = 0, height = 0, border = 0;
    auto image = loadImage(runtime, data, &width, &height, nullptr);
    // [B-15] Surface the failure instead of silently uploading a black texture.
    if (image == nullptr) {
      throw jsi::JSError(
        runtime,
        "EXGL: gl.texImage2D(): failed to load the image referenced by `localUri` "
        "(only file:// URIs pointing to an existing PNG/JPEG file are supported)");
    }
    // [B-6] Decoded images are RGBA8888 — 4 bytes per pixel.
    validatePixelBufferSize(
      runtime,
      "texImage2D",
      width,
      height,
      1,
      type,
      format,
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    if (ctx->unpackFLipY) {
      flipPixels(image.get(), width * bytesPerPixel(type, format), height);
    }
    ctx->addToNextBatch([=] {
      glTexImage2D(target, level, internalformat, width, height, border, format, type, image.get());
    });
  } else {
    throw std::runtime_error("EXGL: Invalid number of arguments to gl.texImage2D()!");
  }
  return nullptr;
}

NATIVE_METHOD(texSubImage2D, 6) {
  CTX();
  auto target = ARG(0, GLenum);
  auto level = ARG(1, GLint);
  auto xoffset = ARG(2, GLint);
  auto yoffset = ARG(3, GLint);
  if (argc == 9) {
    auto width = ARG(4, GLsizei);
    auto height = ARG(5, GLsizei);
    auto format = ARG(6, GLenum);
    auto type = ARG(7, GLenum);
    if (ARG(8, const jsi::Value &).isNull()) {
      ctx->addToNextBatch([=] {
        // [B-1] Allocate a real zero-filled buffer (array form) and call
        // glTexSubImage2D — the previous single-byte allocation memset past
        // its end and passed wrong args to glTexImage2D.
        size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height)
          * static_cast<size_t>(bytesPerPixel(type, format));
        auto empty = std::make_unique<uint8_t[]>(bytes);
        std::memset(empty.get(), 0, bytes);
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, empty.get());
      });
      return nullptr;
    }

    auto data = ARG(8, jsi::Object);

    if (data.isArrayBuffer(runtime) || isTypedArray(runtime, data)) {
      std::vector<uint8_t> vec = rawTypedArray(runtime, std::move(data));
      // [B-6] Validate the upload size (64-bit math) before touching the buffer.
      validatePixelBufferSize(runtime, "texSubImage2D", width, height, 1, type, format, vec.size());
      if (ctx->unpackFLipY) {
        flipPixels(vec.data(), width * bytesPerPixel(type, format), height);
      }
      ctx->addToNextBatch([=, vec{std::move(vec)}] {
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, vec.data());
      });
    } else {
      auto image = loadImage(runtime, data, &width, &height, nullptr);
      // [B-15] Surface the failure instead of silently uploading a black texture.
      if (image == nullptr) {
        throw jsi::JSError(
          runtime,
          "EXGL: gl.texSubImage2D(): failed to load the image referenced by `localUri` "
          "(only file:// URIs pointing to an existing PNG/JPEG file are supported)");
      }
      // [B-6] Decoded images are RGBA8888 — 4 bytes per pixel.
      validatePixelBufferSize(
        runtime,
        "texSubImage2D",
        width,
        height,
        1,
        type,
        format,
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
      if (ctx->unpackFLipY) {
        flipPixels(image.get(), width * bytesPerPixel(type, format), height);
      }
      ctx->addToNextBatch([=] {
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, image.get());
      });
    }
  } else if (argc == 7) {
    auto format = ARG(4, GLenum);
    auto type = ARG(5, GLenum);
    auto data = ARG(6, jsi::Object);
    GLsizei width = 0, height = 0;
    auto image = loadImage(runtime, data, &width, &height, nullptr);
    // [B-15] Surface the failure instead of silently uploading a black texture.
    if (image == nullptr) {
      throw jsi::JSError(
        runtime,
        "EXGL: gl.texSubImage2D(): failed to load the image referenced by `localUri` "
        "(only file:// URIs pointing to an existing PNG/JPEG file are supported)");
    }
    // [B-6] Decoded images are RGBA8888 — 4 bytes per pixel.
    validatePixelBufferSize(
      runtime,
      "texSubImage2D",
      width,
      height,
      1,
      type,
      format,
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    if (ctx->unpackFLipY) {
      flipPixels(image.get(), width * bytesPerPixel(type, format), height);
    }
    ctx->addToNextBatch([=] {
      glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, image.get());
    });
  } else {
    throw std::runtime_error("EXGL: Invalid number of arguments to gl.texSubImage2D()!");
  }
  return nullptr;
}

SIMPLE_NATIVE_METHOD(texParameterf, glTexParameterf); // target, pname, param

SIMPLE_NATIVE_METHOD(texParameteri, glTexParameteri); // target, pname, param

// Textures (WebGL2)
// -----------------

SIMPLE_NATIVE_METHOD(texStorage2D, glTexStorage2D); // target, levels, internalformat, width, height

SIMPLE_NATIVE_METHOD(
  texStorage3D,
  glTexStorage3D); // target, levels, internalformat, width, height, depth

NATIVE_METHOD(texImage3D) {
  CTX();
  auto target = ARG(0, GLenum);
  auto level = ARG(1, GLint);
  auto internalformat = ARG(2, GLint);
  auto width = ARG(3, GLsizei);
  auto height = ARG(4, GLsizei);
  auto depth = ARG(5, GLsizei);
  auto border = ARG(6, GLsizei);
  auto format = ARG(7, GLenum);
  auto type = ARG(8, GLenum);

  if (ARG(9, const jsi::Value &).isNull()) {
    ctx->addToNextBatch([=] {
      glTexImage3D(
        target, level, internalformat, width, height, depth, border, format, type, nullptr);
    });
    return nullptr;
  }
  auto data = ARG(9, jsi::Object);
  auto flip = [&](uint8_t *data) {
    GLubyte *texelLayer = data;
    for (int z = 0; z < depth; z++) {
      flipPixels(texelLayer, width * bytesPerPixel(type, format), height);
      texelLayer += bytesPerPixel(type, format) * width * height;
    }
  };

  if (data.isArrayBuffer(runtime) || isTypedArray(runtime, data)) {
    std::vector<uint8_t> vec = rawTypedArray(runtime, std::move(data));
    // [B-6] Validate the upload size (64-bit math) before touching the buffer.
    validatePixelBufferSize(
      runtime, "texImage3D", width, height, depth, type, format, vec.size());
    if (ctx->unpackFLipY) {
      flip(vec.data());
    }
    ctx->addToNextBatch([=, vec{std::move(vec)}] {
      glTexImage3D(
        target, level, internalformat, width, height, depth, border, format, type, vec.data());
    });
  } else {
    auto image = loadImage(runtime, data, &width, &height, nullptr);
    // [B-15] Surface the failure instead of silently uploading a black texture.
    if (image == nullptr) {
      throw jsi::JSError(
        runtime,
        "EXGL: gl.texImage3D(): failed to load the image referenced by `localUri` "
        "(only file:// URIs pointing to an existing PNG/JPEG file are supported)");
    }
    // [B-6] Decoded images are RGBA8888 — 4 bytes per pixel.
    validatePixelBufferSize(
      runtime,
      "texImage3D",
      width,
      height,
      depth,
      type,
      format,
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    if (ctx->unpackFLipY) {
      flip(image.get());
    }
    ctx->addToNextBatch([=] {
      glTexImage3D(
        target, level, internalformat, width, height, depth, border, format, type, image.get());
    });
  }
  return nullptr;
}

NATIVE_METHOD(texSubImage3D) {
  CTX();
  auto target = ARG(0, GLenum);
  auto level = ARG(1, GLint);
  auto xoffset = ARG(2, GLint);
  auto yoffset = ARG(3, GLint);
  auto zoffset = ARG(4, GLint);
  auto width = ARG(5, GLsizei);
  auto height = ARG(6, GLsizei);
  auto depth = ARG(7, GLsizei);
  auto format = ARG(8, GLenum);
  auto type = ARG(9, GLenum);

  if (ARG(10, const jsi::Value &).isNull()) {
    ctx->addToNextBatch([=] {
      // [B-2] Array allocation — the previous single-byte allocation memset
      // past its end when width*height*depth*bpp > 1.
      size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height)
        * static_cast<size_t>(depth) * static_cast<size_t>(bytesPerPixel(type, format));
      auto empty = std::make_unique<uint8_t[]>(bytes);
      std::memset(empty.get(), 0, bytes);
      glTexSubImage3D(
        target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, empty.get());
    });
    return nullptr;
  }
  auto data = ARG(10, jsi::Object);
  auto flip = [&](uint8_t *data) {
    GLubyte *texelLayer = data;
    for (int z = 0; z < depth; z++) {
      flipPixels(texelLayer, width * bytesPerPixel(type, format), height);
      texelLayer += bytesPerPixel(type, format) * width * height;
    }
  };

  if (data.isArrayBuffer(runtime) || isTypedArray(runtime, data)) {
    std::vector<uint8_t> vec = rawTypedArray(runtime, std::move(data));
    // [B-6] Validate the upload size (64-bit math) before touching the buffer.
    validatePixelBufferSize(
      runtime, "texSubImage3D", width, height, depth, type, format, vec.size());
    if (ctx->unpackFLipY) {
      flip(vec.data());
    }
    ctx->addToNextBatch([=, vec{std::move(vec)}] {
      glTexSubImage3D(
        target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, vec.data());
    });
  } else {
    auto image = loadImage(runtime, data, &width, &height, nullptr);
    // [B-15] Surface the failure instead of silently uploading a black texture.
    if (image == nullptr) {
      throw jsi::JSError(
        runtime,
        "EXGL: gl.texSubImage3D(): failed to load the image referenced by `localUri` "
        "(only file:// URIs pointing to an existing PNG/JPEG file are supported)");
    }
    // [B-6] Decoded images are RGBA8888 — 4 bytes per pixel.
    validatePixelBufferSize(
      runtime,
      "texSubImage3D",
      width,
      height,
      depth,
      type,
      format,
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    if (ctx->unpackFLipY) {
      flip(image.get());
    }
    ctx->addToNextBatch([=] {
      glTexSubImage3D(
        target,
        level,
        xoffset,
        yoffset,
        zoffset,
        width,
        height,
        depth,
        format,
        type,
        image.get());
    });
  }
  return nullptr;
}

SIMPLE_NATIVE_METHOD(
  copyTexSubImage3D,
  glCopyTexSubImage3D); // target, level, xoffset, yoffset, zoffset, x, y, width, height

UNIMPL_NATIVE_METHOD(compressedTexImage3D)

UNIMPL_NATIVE_METHOD(compressedTexSubImage3D)

// Programs and shaders
// --------------------

NATIVE_METHOD(attachShader) {
  CTX();
  auto program = ARG(0, EXWebGLClass);
  auto shader = ARG(1, EXWebGLClass);
  ctx->addToNextBatch(
    [=] { glAttachShader(ctx->lookupObject(program), ctx->lookupObject(shader)); });
  return nullptr;
}

NATIVE_METHOD(bindAttribLocation) {
  CTX();
  auto program = ARG(0, EXWebGLClass);
  auto index = ARG(1, GLuint);
  auto name = ARG(2, std::string);
  ctx->addToNextBatch([=, name{std::move(name)}] {
    glBindAttribLocation(ctx->lookupObject(program), index, name.c_str());
  });
  return nullptr;
}

NATIVE_METHOD(compileShader) {
  CTX();
  auto shader = ARG(0, EXWebGLClass);
  ctx->addToNextBatch([=] { glCompileShader(ctx->lookupObject(shader)); });
  return nullptr;
}

NATIVE_METHOD(createProgram) {
  CTX();
  return exglCreateObject(ctx, runtime, glCreateProgram, EXWebGLClass::WebGLProgram);
}

NATIVE_METHOD(createShader) {
  CTX();
  auto type = ARG(0, GLenum);
  if (type == GL_VERTEX_SHADER || type == GL_FRAGMENT_SHADER) {
    return exglCreateObject(
      ctx, runtime, std::bind(glCreateShader, type), EXWebGLClass::WebGLShader);
  } else {
    throw std::runtime_error("unknown shader type passed to function");
  }
}

NATIVE_METHOD(deleteProgram) {
  CTX();
  return exglDeleteObject(ctx, ARG(0, EXWebGLClass), glDeleteProgram);
}

NATIVE_METHOD(deleteShader) {
  CTX();
  return exglDeleteObject(ctx, ARG(0, EXWebGLClass), glDeleteShader);
}

NATIVE_METHOD(detachShader) {
  CTX();
  auto program = ARG(0, EXWebGLClass);
  auto shader = ARG(1, EXWebGLClass);
  ctx->addToNextBatch(
    [=] { glDetachShader(ctx->lookupObject(program), ctx->lookupObject(shader)); });
  return nullptr;
}

NATIVE_METHOD(getAttachedShaders) {
  CTX();
  auto fProgram = ARG(0, EXWebGLClass);

  // [B-9] Zero-initialized query outputs + program validity guard: an invalid
  // program makes the glGet* calls skip their output params.
  GLint count = 0;
  std::vector<EXGLObjectId> exglResults;
  bool validProgram = false;
  ctx->addBlockingToNextBatch([&] {
    GLuint program = ctx->lookupObject(fProgram);
    if (program == 0 || !glIsProgram(program)) {
      return;
    }
    validProgram = true;
    glGetProgramiv(program, GL_ATTACHED_SHADERS, &count);
    std::vector<GLuint> glResults(count);
    glGetAttachedShaders(program, count, nullptr, glResults.data());
    // [B-10] Reverse id lookup runs on the GL thread — `ctx->objects` is only
    // safe to touch there (same-thread as mapObject).
    exglResults.reserve(count);
    for (auto i = 0; i < count; ++i) {
      EXGLObjectId exglObjId = 0;
      for (const auto &pair: ctx->objects) {
        if (pair.second == glResults[i]) {
          exglObjId = pair.first;
        }
      }
      exglResults.push_back(exglObjId);
    }
  });

  if (!validProgram) {
    // WebGL semantics: querying an invalid program yields null.
    return nullptr;
  }

  jsi::Array jsResults(runtime, exglResults.size());
  for (size_t i = 0; i < exglResults.size(); ++i) {
    if (exglResults[i] == 0) {
      throw std::runtime_error(
        "EXGL: Internal error: couldn't find EXGLObjectId "
        "associated with shader in getAttachedShaders()!");
    }
    jsResults.setValueAtIndex(
      runtime,
      i,
      createWebGLObject(runtime, EXWebGLClass::WebGLShader, {static_cast<double>(exglResults[i])}));
  }
  return jsResults;
}

NATIVE_METHOD(getProgramParameter) {
  CTX();
  auto fProgram = ARG(0, EXWebGLClass);
  auto pname = ARG(1, GLenum);
  GLint glResult = 0; // [B-9] invalid program leaves the output param untouched
  ctx->addBlockingToNextBatch([&] {
    GLuint program = ctx->lookupObject(fProgram);
    if (glIsProgram(program)) {
      glGetProgramiv(program, pname, &glResult);
    }
  });
  if (pname == GL_DELETE_STATUS || pname == GL_LINK_STATUS || pname == GL_VALIDATE_STATUS) {
    return glResult == GL_TRUE;
  } else {
    return glResult;
  }
}

NATIVE_METHOD(getShaderParameter) {
  CTX();
  auto fShader = ARG(0, EXWebGLClass);
  auto pname = ARG(1, GLenum);
  GLint glResult = 0; // [B-9] invalid shader leaves the output param untouched
  ctx->addBlockingToNextBatch([&] {
    GLuint shader = ctx->lookupObject(fShader);
    if (glIsShader(shader)) {
      glGetShaderiv(shader, pname, &glResult);
    }
  });
  if (pname == GL_DELETE_STATUS || pname == GL_COMPILE_STATUS) {
    return glResult == GL_TRUE;
  } else {
    return glResult;
  }
}

NATIVE_METHOD(getShaderPrecisionFormat) {
  CTX();
  auto shaderType = ARG(0, GLenum);
  auto precisionType = ARG(1, GLenum);

  GLint range[2] = {0, 0}, precision = 0; // [B-9] zero-init GL query outputs
  ctx->addBlockingToNextBatch(
    [&] { glGetShaderPrecisionFormat(shaderType, precisionType, range, &precision); });

  jsi::Object jsResult =
    createWebGLObject(runtime, EXWebGLClass::WebGLShaderPrecisionFormat, {}).asObject(runtime);
  jsResult.setProperty(runtime, "rangeMin", jsi::Value(range[0]));
  jsResult.setProperty(runtime, "rangeMax", jsi::Value(range[1]));
  jsResult.setProperty(runtime, "precision", jsi::Value(precision));
  return jsResult;
}

NATIVE_METHOD(getProgramInfoLog) {
  CTX();
  auto fObj = ARG(0, EXWebGLClass);
  std::string str;
  ctx->addBlockingToNextBatch([&] {
    GLuint obj = ctx->lookupObject(fObj);
    GLint length = 0; // [B-9] zero-init: an invalid program skips the query write
    if (glIsProgram(obj)) {
      glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &length);
    }
    str.resize(length > 0 ? length - 1 : 0);
    if (length > 0) {
      glGetProgramInfoLog(obj, length, nullptr, &str[0]);
    }
  });
  return jsi::String::createFromUtf8(runtime, str);
}

NATIVE_METHOD(getShaderInfoLog) {
  CTX();
  auto fObj = ARG(0, EXWebGLClass);
  std::string str;
  ctx->addBlockingToNextBatch([&] {
    GLuint obj = ctx->lookupObject(fObj);
    GLint length = 0; // [B-9] zero-init: an invalid shader skips the query write
    if (glIsShader(obj)) {
      glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &length);
    }
    str.resize(length > 0 ? length - 1 : 0);
    if (length > 0) {
      glGetShaderInfoLog(obj, length, nullptr, &str[0]);
    }
  });
  return jsi::String::createFromUtf8(runtime, str);
}

NATIVE_METHOD(getShaderSource) {
  CTX();
  auto fObj = ARG(0, EXWebGLClass);
  std::string str;
  ctx->addBlockingToNextBatch([&] {
    GLuint obj = ctx->lookupObject(fObj);
    GLint length = 0; // [B-9] zero-init: an invalid shader skips the query write
    if (glIsShader(obj)) {
      glGetShaderiv(obj, GL_SHADER_SOURCE_LENGTH, &length);
    }
    str.resize(length > 0 ? length - 1 : 0);
    if (length > 0) {
      glGetShaderSource(obj, length, nullptr, &str[0]);
    }
  });
  return jsi::String::createFromUtf8(runtime, str);
}

NATIVE_METHOD(isShader) {
  CTX();
  return exglIsObject(ctx, ARG(0, EXWebGLClass), glIsShader);
}

NATIVE_METHOD(isProgram) {
  CTX();
  return exglIsObject(ctx, ARG(0, EXWebGLClass), glIsProgram);
}

NATIVE_METHOD(linkProgram) {
  CTX();
  auto fProgram = ARG(0, EXWebGLClass);
  ctx->addToNextBatch([=] { glLinkProgram(ctx->lookupObject(fProgram)); });
  return nullptr;
}

NATIVE_METHOD(shaderSource) {
  CTX();
  auto fShader = ARG(0, EXWebGLClass);
  auto str = ARG(1, std::string);
  ctx->addToNextBatch([=, str{std::move(str)}] {
    const char *cstr = str.c_str();
    glShaderSource(ctx->lookupObject(fShader), 1, &cstr, nullptr);
  });
  return nullptr;
}

NATIVE_METHOD(useProgram) {
  CTX();
  auto program = ARG(0, EXWebGLClass);
  ctx->addToNextBatch([=] { glUseProgram(ctx->lookupObject(program)); });
  return nullptr;
}

NATIVE_METHOD(validateProgram) {
  CTX();
  auto program = ARG(0, EXWebGLClass);
  ctx->addToNextBatch([=] { glValidateProgram(ctx->lookupObject(program)); });
  return nullptr;
}

// Programs and shaders (WebGL2)

NATIVE_METHOD(getFragDataLocation) {
  CTX();
  auto program = ARG(0, EXWebGLClass);
  auto name = ARG(1, std::string);
  GLint location = -1; // [B-9] zero-init: an invalid program leaves the value untouched
  ctx->addBlockingToNextBatch([&] {
    GLuint programHandle = ctx->lookupObject(program);
    location = glIsProgram(programHandle) ? glGetFragDataLocation(programHandle, name.c_str()) : -1;
  });
  return location == -1 ? jsi::Value::null() : jsi::Value(location);
}


} // namespace method
} // namespace gl_cpp
} // namespace expo
