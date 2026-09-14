/**
 * ArkTS entry package for the expo-gl HarmonyOS adaptation.
 *
 * Registers the ExpoGLView Fabric component (builder + descriptor wrapper
 * factory). The `ExpoGL` TurboModule is implemented on the C++ side
 * (ExpoGLModule.h in cpp/) and registered through the package of the same
 * name in cpp/ExpoGlPackage.h — nothing to register here for it.
 */
import {
  RNOHPackage,
  ComponentBuilderContext,
} from '@rnoh/react-native-openharmony';
import type {
  DescriptorWrapperFactoryByDescriptorTypeCtx,
  DescriptorWrapperFactoryByDescriptorType,
} from '@rnoh/react-native-openharmony/ts';
import { RNC } from './generated/ts';
import { ExpoGLView } from './ExpoGLView';

@Builder
export function buildExpoGLView(ctx: ComponentBuilderContext) {
  ExpoGLView({ ctx: ctx.rnComponentContext, tag: ctx.tag })
}

export class ExpoGlPackage extends RNOHPackage {
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
