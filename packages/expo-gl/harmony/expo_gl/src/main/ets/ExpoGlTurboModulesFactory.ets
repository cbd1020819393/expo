/**
 * ArkTS entry package for the expo-gl HarmonyOS adaptation.
 *
 * Registers the ExpoGLView Fabric component (builder + descriptor wrapper
 * factory). The `ExpoGL` TurboModule is implemented on the C++ side
 * (ExpoGLModule.h in cpp/) and registered through the package of the same
 * name in cpp/ExpoGlPackage.h — nothing to register here for it.
 *
 * The `ExpoAsset` TurboModule (ExpoAssetModule.ets) is implemented here in
 * ArkTS: it belongs to the gl-react dependency chain (texture loading via
 * expo-asset) and lives in this har so customer apps don't need to depend
 * on an extra native package for it. Its JS consumer is the hand-written
 * `@react-native-ohos/expo-asset` entry, which resolves it through
 * `requireNativeModule('ExpoAsset')`.
 */
import {
  RNOHPackage,
  ComponentBuilderContext,
  UITurboModule,
  UITurboModuleContext,
} from '@rnoh/react-native-openharmony';
import type {
  DescriptorWrapperFactoryByDescriptorTypeCtx,
  DescriptorWrapperFactoryByDescriptorType,
} from '@rnoh/react-native-openharmony/ts';
import { RNC } from './generated/ts';
import { ExpoGLView } from './ExpoGLView';
import { ExpoAssetModule } from './ExpoAssetModule';

@Builder
export function buildExpoGLView(ctx: ComponentBuilderContext) {
  ExpoGLView({ ctx: ctx.rnComponentContext, tag: ctx.tag })
}

export class ExpoGlPackage extends RNOHPackage {
  // ExpoAsset (pure ArkTS TurboModule) MUST be registered through
  // getUITurboModuleFactoryByNameMap — the same path every pure-ArkTS module
  // without C++ codegen artifacts uses. Registering it via the legacy
  // createTurboModulesFactory override left it invisible to the C++ side's
  // ArkTS fallback lookup ("Cannot find native module 'ExpoAsset'").
  getUITurboModuleFactoryByNameMap(): Map<string, (ctx: UITurboModuleContext) => UITurboModule | null> {
    return new Map<string, (ctx: UITurboModuleContext) => UITurboModule | null>()
      .set('ExpoAsset', (ctx: UITurboModuleContext): UITurboModule => new ExpoAssetModule(ctx));
  }

  createDescriptorWrapperFactoryByDescriptorType(
    ctx: DescriptorWrapperFactoryByDescriptorTypeCtx
  ): DescriptorWrapperFactoryByDescriptorType {
    const result: DescriptorWrapperFactoryByDescriptorType = {}
    result[RNC.ExpoGLView.NAME] = (ctx) => new RNC.ExpoGLView.DescriptorWrapper(ctx.descriptor)
    return result
  }

  createWrappedCustomRNComponentBuilderByComponentNameMap(): Map<string, WrappedBuilder<[ComponentBuilderContext]>> {
    return new Map().set(ExpoGLView.NAME, wrapBuilder(buildExpoGLView))
  }
}
