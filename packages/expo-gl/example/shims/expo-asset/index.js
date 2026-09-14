/**
 * Minimal expo-asset shim for HarmonyOS (RNOH) gl-react verification.
 * webgltexture-loader-expo requires expo-asset for URI asset textures;
 * that path has no HarmonyOS native side yet and is not exercised by this
 * demo. Provide the API shape so metro can bundle.
 */
class Asset {
  constructor({ localUri = null, width = 0, height = 0 } = {}) {
    this.localUri = localUri;
    this.width = width;
    this.height = height;
  }
  async downloadAsync() {
    return this;
  }
  static fromModule() {
    return new Asset();
  }
  static fromURI(uri) {
    return new Asset({ localUri: uri });
  }
}
module.exports = { Asset };
