/**
 * This source code is licensed under the MIT license found in the
 * LICENSE-MIT file in the root directory of this source tree.
 */

const path = require('path');
const {mergeConfig, getDefaultConfig} = require('@react-native/metro-config');
const {createHarmonyMetroConfig} = require('@react-native-oh/react-native-harmony/metro.config');

/**
 * @type {import("metro-config").ConfigT}
 */
const config = {
  transformer: {
    getTransformOptions: async () => ({
      transform: {
        experimentalImportSupport: false,
        inlineRequires: true,
      },
    }),
  },
  resolver: {
    // gl-react 的纹理加载器（webgltexture-loader-expo）引用 expo-asset /
    // expo-modules-core，这两者的原生侧尚未鸿蒙化且本验证 Demo 不使用该路径，
    // 用本地 shim 提供最小 API 形状（见 shims/ 目录内注释）。
    extraNodeModules: {
      'expo-asset': path.resolve(__dirname, 'shims', 'expo-asset'),
      'expo-modules-core': path.resolve(__dirname, 'shims', 'expo-modules-core'),
    },
  },
};

module.exports = mergeConfig(getDefaultConfig(__dirname), createHarmonyMetroConfig({
  reactNativeHarmonyPackageName: '@react-native-oh/react-native-harmony',
}), config);