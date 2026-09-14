# @oh-rn/expo-gl for HarmonyOS

本项目基于 [expo-gl](https://github.com/expo/expo) 开发，为 React Native 鸿蒙（OpenHarmony）适配版本。

## 版本对应关系

| 鸿蒙适配包版本 | 原始库版本 | 支持 RN 版本 | Autolink | 编译 API 版本 |
| ------------ | ---------- | ------------ | -------- | ------------- |
| 见发布记录 | 58.0.0 | 0.72+ | 是 | API12+ |

## 安装

```bash
npm install @oh-rn/expo-gl
```

## 使用

```tsx
import React, { useRef } from 'react';
import { StyleSheet } from 'react-native';
import { GLView } from 'expo-gl';

// 1. 视图渲染：<GLView> 挂载后回调 onContextCreate，拿到 WebGL 上下文
function GLViewScreen() {
  const glRef = useRef(null);
  return (
    <GLView
      style={styles.gl}
      onContextCreate={(gl) => {
        glRef.current = gl;
        gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
        gl.clearColor(0, 0, 1, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.endFrameEXP(); // 每帧结束后调用，驱动上屏
      }}
    />
  );
}

// 2. 无头上下文（无视图渲染，配合自定义 framebuffer + 快照）
const gl = await GLView.createContextAsync();
//   ...texImage2D / framebufferTexture2D / draw...
const snapshot = await GLView.takeSnapshotAsync(gl, {
  framebuffer: { id: fbo.id },
  format: 'png',
  compress: 1.0,
});
// snapshot = { uri, localUri, width, height }，文件写入应用沙箱 cache 目录
const destroyed = await GLView.destroyContextAsync(gl);
```

```tsx
// （接上例）样式
const styles = StyleSheet.create({ gl: { flex: 1 } });
```

> import 时使用原库名 `'expo-gl'`，而非鸿蒙包名（RNOH 的 metro alias 会自动重定向到 `@oh-rn/expo-gl`）。

**平台差异**：
- `takeSnapshotAsync` 的 `format: 'webp'` 在 HarmonyOS 上降级导出 `.png` 并打印告警（与 iOS 行为一致），建议用平台分支代码显式处理。
- `msaaSamples` 为 iOS 专属（多重采样），HarmonyOS 上不生效（与 Android 行为一致）。
- GL 上下文经 JSI 直装到 JS 全局对象，**远程调试模式下不可用**（与原库限制相同，此时抛 `ERR_GL_NOT_AVAILABLE`）。

**权限要求**：
- 无。快照写入应用沙箱 `cache` 目录（`cacheDir/GLView/`），无需声明任何 `ohos.permission`。

## Link

| 版本 | 是否支持 Autolink |
|------|------------------|
| 当前版本 | 是 |

如使用版本支持 Autolink 且工程已接入，可跳过手动配置。

<details>
<summary>Manual Link 配置</summary>

> **说明**：本模块需要同时在 C++ 侧和 ETS 侧注册 Package。

### 1. Overrides RN SDK

在工程根目录 `oh-package.json5` 添加：

```json
{
  "overrides": {
    "@rnoh/react-native-openharmony": "./react_native_openharmony"
  }
}
```

### 2. 引入原生端依赖

打开 `entry/oh-package.json5`，添加：

```json
"dependencies": {
  "@oh-rn/expo-gl": "file:../../node_modules/@oh-rn/expo-gl/harmony/expo_gl.har"
}
```

执行 `ohpm install`。

### 3. 配置 CMakeLists

打开 `entry/src/main/cpp/CMakeLists.txt`，添加：

```cmake
set(OH_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../../../oh_modules")

add_subdirectory("${OH_MODULES}/@oh-rn/expo-gl/src/main/cpp" ./expo_gl)

target_link_libraries(rnoh_app PUBLIC expo_gl)
```

### 4. 注册 Package（C++ 侧）

打开 `entry/src/main/cpp/PackageProvider.cpp`，添加：

```cpp
#include "ExpoGlPackage.h"

std::vector<std::shared_ptr<Package>> PackageProvider::getPackages(Package::Context ctx) {
    return {
        std::make_shared<ExpoGlPackage>(ctx),
    };
}
```

### 5. 注册 Package（ETS 侧）

打开 `entry/src/main/ets/RNPackagesFactory.ets`，添加：

```typescript
import { ExpoGlPackage } from '@oh-rn/expo-gl/ts';

export function createRNPackages(ctx: RNPackageContext): RNPackage[] {
  return [
    new ExpoGlPackage(ctx),
  ];
}
```

</details>

## 属性 / API

| API | 描述 | 参数 | 返回值 | HarmonyOS 支持 |
|-----|------|------|--------|----------------|
| GLView（组件） | OpenGL ES 渲染目标视图，挂载后创建 GL 上下文 | style、onContextCreate 等 View 属性 | — | ✅ 完全支持 |
| onContextCreate | GL 上下文创建完成回调 | (gl: ExpoWebGLRenderingContext) => void | — | ✅ 完全支持 |
| GLView.createContextAsync | 创建无头上下文 | 无 | Promise\<ExpoWebGLRenderingContext\> | ✅ 完全支持 |
| GLView.destroyContextAsync | 销毁指定上下文 | exgl: ExpoWebGLRenderingContext \| number | Promise\<boolean\>（上下文不存在返回 false） | ✅ 完全支持 |
| GLView.takeSnapshotAsync | 将 framebuffer 快照编码为图片文件 | exgl, options: { flip?, framebuffer?, rect?, format?, compress? } | Promise\<{ uri, localUri, width, height }\> | ⚠️ 部分支持（`format: 'webp'` 降级为 png） |
| gl.endFrameEXP | 结束一帧并驱动上屏 | 无 | void | ✅ 完全支持 |
| gl.flushEXP | 在 GL 线程执行已入队命令（不上屏） | 无 | void | ✅ 完全支持 |
| gl.__expoSetLogging | 开关 GL 调用日志 | option: GLLoggingOption | void | ✅ 完全支持 |
| gl.contextId | 当前 GL 上下文 id | — | number | ✅ 完全支持 |
| WebGL 2.0 子集命令（约 300 个，texImage2D/createProgram 等） | 经 JSI 直装在 `global.__EXGLContexts[ctxId]`，不经过桥 | — | — | ✅ 完全支持（远程调试模式下不可用） |
| GLView#destroyObjectAsync | 销毁已注册的 GL 对象 | glObject: { id: number } | Promise\<boolean\>（未知 id 返回 false，与原库一致） | ✅ 完全支持 |
| GLView#getWorkletContext | 取 worklet 线程的 GL 上下文（deprecated，改用全局 getWorkletContext） | contextId: number | ExpoWebGLRenderingContext \| undefined | ❌ 不支持（worklet 实验能力在鸿蒙首期关闭，抛 `Worklet runtime is not available`，与原库无 worklet 运行时时行为一致） |
| getWorkletContext（全局导出） | 同上（worklet 场景专用） | contextId: number | ExpoWebGLRenderingContext \| undefined | ❌ 不支持（同上） |
| GLView#createCameraTextureAsync | 将相机画面注册为 GL 纹理 | cameraRefOrHandle | Promise\<WebGLTexture\> | ❌ 不支持（依赖 expo-camera 的鸿蒙适配，JS 侧抛 UnavailabilityError） |
| msaaSamples（prop） | 多重采样数 | number（默认 4） | — | ❌ 不生效（iOS 专属能力，鸿蒙忽略，与 Android 一致） |
| enableExperimentalWorkletSupport（prop） | 开启 worklet 线程访问 gl | boolean（默认 false） | — | ⚠️ 仅保留 prop（变更时 console.warn，实际不生效） |

### 平台差异
- `takeSnapshotAsync`：`format: 'webp'` 输入统一导出 `.png` 并打印 hilog 告警（对齐 iOS `EXGLContext.mm` 的降级行为）；`flip`/`rect`/`framebuffer`/`compress` 语义与 Android/iOS 逐项一致。
- `msaaSamples`：原库即 iOS 专属（APPLE_framebuffer_multisample），鸿蒙与 Android 一样不实现。
- GL 对象经 JSI 注入 `global.__EXGLContexts`，因此与原库一样不支持远程调试（Chrome Debug 模式下 `getGl` 抛 `ERR_GL_NOT_AVAILABLE`）。

### 未实现功能
| API | 原因 |
|-----|------|
| createCameraTextureAsync | 依赖 expo-camera 的 `CameraViewInterface`（相机视图纹理接口），expo-camera 鸿蒙适配未就绪；JS 侧有原库同款 UnavailabilityError 保护，后续接入只需经 `eglGetNativeClientBufferANDROID` 注册纹理生产者 |
| getWorkletContext / enableExperimentalWorkletSupport 实际生效 | 依赖 reanimated 鸿蒙版的 worklet 运行时，首期关闭；C++ 侧 `EXGLContextPrepareWorklet` 已完整编译在内，后续恢复只需 JS 层重新引入 reanimated 软依赖 |

### 使用限制
- 无需任何 `ohos.permission`（快照写应用沙箱 cache 目录）。
- 每个 `<GLView>` 挂载时会创建独立 EGL 上下文（XComponent surface 驱动）；不使用时请卸载组件或调用 `destroyContextAsync` 释放。

## 快速验证（运行 Example）

### 前置条件

| 依赖 | 版本要求 |
|------|----------|
| Node.js | >= 18 |
| DevEco Studio | 5.0+ / 6.0+ |
| HarmonyOS SDK | API 12+ |

### 运行步骤

**1. 克隆仓库**

```bash
git clone <仓库地址>
cd <仓库目录>
```

**2. 安装依赖并构建**

```bash
npm install --legacy-peer-deps
npm pack           # 生成 tgz 包（会自动触发 prepare 构建 JS 产物）
```

**3. 进入 example 目录，安装依赖**

```bash
cd example
npm install --legacy-peer-deps
```

**4. 生成 JS Bundle**

```bash
npm run dev
```

产物：`harmony/entry/src/main/resources/rawfile/bundle.harmony.js`

**5. 用 DevEco Studio 打开鸿蒙工程**

- 打开 DevEco Studio
- 选择 `example/harmony` 目录
- 等待 Sync 完成

**6. 编译并运行 HAP**

在 DevEco Studio 中点击运行按钮，将 HAP 安装到设备/模拟器。

> **注意**：Example 中已预置插件依赖和 Package 注册，无需手动配置 Link。

## 约束与限制

### 兼容性

- RNOH: 0.72+
- HarmonyOS SDK: API 12+
- DevEco Studio: 5.0+

## 遗留问题

无（或列出已知问题）

## 开源协议

本项目基于 [MIT](https://github.com/expo/expo/blob/main/LICENSE) 协议，详见 [LICENSE](./LICENSE) 文件。