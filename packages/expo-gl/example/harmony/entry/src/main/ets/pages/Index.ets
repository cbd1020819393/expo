/**
 * This source code is licensed under the MIT license found in the
 * LICENSE-MIT file in the root directory of this source tree.
 */

import {
  AnyJSBundleProvider,
  ComponentBuilderContext,
  FileJSBundleProvider,
  MetroJSBundleProvider,
  ResourceJSBundleProvider,
  RNApp,
  RNOHErrorDialog,
  RNOHLogger,
  TraceJSBundleProviderDecorator,
  RNOHCoreContext
} from '@rnoh/react-native-openharmony';
import { createRNPackages } from '../RNPackagesFactory';
import { ExpoGLView } from '@oh-rn/expo-gl';
// GEN:FABRIC_IMPORTS_BEGIN
// GEN:FABRIC_IMPORTS_END

// GEN:FABRIC_ARK_CONST_BEGIN
// GEN:FABRIC_ARK_CONST_END

@Builder
export function buildCustomRNComponent(ctx: ComponentBuilderContext) {
// GEN:FABRIC_BUILDER_BODY_BEGIN
  Stack() {
    if (ctx.componentName === ExpoGLView.NAME) {
      ExpoGLView({
        ctx: ctx.rnComponentContext,
        tag: ctx.tag
      })
    }
  }
  .position({ x: 0, y: 0 })
// GEN:FABRIC_BUILDER_BODY_END
}

const wrappedCustomRNComponentBuilder = wrapBuilder(buildCustomRNComponent)

@Entry
@Component
struct Index {
  @StorageLink('RNOHCoreContext') private rnohCoreContext: RNOHCoreContext | undefined = undefined
  @State shouldShow: boolean = false
  private logger!: RNOHLogger
  // [ExpoGLProbe] isolated XComponent outside RNOH — decides whether onLoad
  // works at all on this device or whether the RNOH embedding breaks it.
  private testController: XComponentController = new XComponentController()

  aboutToAppear() {
    this.logger = this.rnohCoreContext!.logger.clone("Index")
    const stopTracing = this.logger.clone("aboutToAppear").startTracing();

    this.shouldShow = true
    stopTracing();
  }

  onBackPress(): boolean | undefined {
    // NOTE: this is required since `Ability`'s `onBackPressed` function always
    // terminates or puts the app in the background, but we want Ark to ignore it completely
    // when handled by RN
    this.rnohCoreContext!.dispatchBackPress()
    return true
  }

  build() {
    Column() {
      if (this.rnohCoreContext && this.shouldShow) {
        if (this.rnohCoreContext?.isDebugModeEnabled) {
          RNOHErrorDialog({ ctx: this.rnohCoreContext })
        }
        RNApp({
          rnInstanceConfig: {
            createRNPackages,
            enableNDKTextMeasuring: true,
            enableBackgroundExecutor: false,
            enableCAPIArchitecture: true,
            arkTsComponentNames: [ExpoGLView.NAME]
          },
          initialProps: { "foo": "bar" } as Record<string, string>,
          appKey: "AwesomeProject",
          wrappedCustomRNComponentBuilder: wrappedCustomRNComponentBuilder,
          onSetUp: (rnInstance) => {
            rnInstance.enableFeatureFlag("ENABLE_RN_INSTANCE_CLEAN_UP")
          },
          jsBundleProvider: new TraceJSBundleProviderDecorator(
            new AnyJSBundleProvider([
              new MetroJSBundleProvider(),
              // NOTE: to load the bundle from file, place it in
              // `/data/app/el2/100/base/com.rnoh.tester/files/bundle.harmony.js`
              // on your device. The path mismatch is due to app sandboxing on OpenHarmony
              new FileJSBundleProvider('/data/storage/el2/base/files/bundle.harmony.js'),
              new ResourceJSBundleProvider(this.rnohCoreContext.uiAbilityContext.resourceManager, 'hermes_bundle.hbc'),
              new ResourceJSBundleProvider(this.rnohCoreContext.uiAbilityContext.resourceManager, 'bundle.harmony.js')
            ]),
            this.rnohCoreContext.logger),
        })
      }
      // [ExpoGLProbe] isolated XComponent outside RNOH (80vp strip at bottom)
      XComponent({
        id: 'isolated_probe_xc',
        type: 'surface',
        controller: this.testController
      })
        .width('100%')
        .height(80)
        .onLoad(() => {
          try {
            console.info('[ExpoGLProbe] Isolated onLoad fired');
            console.info('[ExpoGLProbe] Isolated surfaceId=' + this.testController.getXComponentSurfaceId());
          } catch (e) {
            console.error('[ExpoGLProbe] Isolated onLoad THREW: ' + String(e));
          }
        })
    }
    .height('100%')
    .width('100%')
  }
}
