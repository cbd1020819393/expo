/**
 * Minimal expo-modules-core shim for HarmonyOS (RNOH) gl-react verification.
 * webgltexture-loader-expo only probes NativeModulesProxy.ExponentGLObjectManager
 * (deprecated GL object texture path) — absent here, matching its own
 * "available" detection which yields false on unsupported platforms.
 */
const NativeModulesProxy = {};
module.exports = { NativeModulesProxy };
