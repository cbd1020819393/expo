/**
 * gl-react × expo-gl(HarmonyOS 适配版) 组合验证 Demo
 *
 * 验证目标：Surface(gl-react-expo) → GLView(@oh-rn/expo-gl 适配版) →
 *           RNOH TurboModule → GLES，全链路真实渲染。
 * 预期表现：屏幕出现 300×300 渐变色块（左上黑 → 右下青蓝），
 *           且没有"渲染异常"文本。
 * 原始适配验证页备份在同目录 App.rn-ohos-backup.tsx。
 */

import React, {Component} from 'react';
import {SafeAreaView, StyleSheet, Text, View} from 'react-native';
import {Shaders, GLSL, Node} from 'gl-react';
import {Surface} from 'gl-react-expo';

const shaders = Shaders.create({
  gradient: {
    frag: GLSL`
      precision highp float;
      varying vec2 uv;
      void main() {
        gl_FragColor = vec4(uv.x, uv.y, 0.5, 1.0);
      }
    `,
  },
});

class ErrorBoundary extends Component<
  {children: any},
  {err: string | null}
> {
  state = {err: null as string | null};
  static getDerivedStateFromError(e: any) {
    return {err: String((e && e.message) || e)};
  }
  render() {
    if (this.state.err) {
      return <Text style={styles.err}>渲染异常: {this.state.err}</Text>;
    }
    return this.props.children;
  }
}

function App(): React.JSX.Element {
  return (
    <SafeAreaView style={styles.root}>
      <Text style={styles.title}>gl-react × expo-gl 组合验证</Text>
      <Text style={styles.hint}>
        预期：300×300 渐变色块（左上黑 → 右下青蓝）
      </Text>
      <View style={styles.box}>
        <ErrorBoundary>
          <Surface style={{width: 300, height: 300}}>
            <Node shader={shaders.gradient} />
          </Surface>
        </ErrorBoundary>
      </View>
      <Text style={styles.note}>
        看到渐变色块且无"渲染异常"文本 = gl-react 全链路在鸿蒙打通
      </Text>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#fff', alignItems: 'center'},
  title: {fontSize: 20, fontWeight: 'bold', marginTop: 24, color: '#111'},
  hint: {fontSize: 14, color: '#666', marginTop: 8},
  box: {marginTop: 24, borderWidth: 1, borderColor: '#ddd'},
  note: {fontSize: 13, color: '#999', marginTop: 16, paddingHorizontal: 24},
  err: {color: '#c00', marginTop: 24, paddingHorizontal: 24},
});

export default App;
