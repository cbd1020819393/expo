/**
 * Example app for the expo-gl HarmonyOS (RNOH) adaptation.
 *
 * Covers the library's public API surface with real calls and real results:
 *  - GLView rendering (onContextCreate + animated WebGL triangle)
 *  - takeSnapshotAsync options (format / compress / flip / webp downgrade)
 *  - headless context: createContextAsync + own FBO + snapshot + destroy
 *  - destroyObjectAsync (unknown id -> false, same as upstream)
 *  - capabilities that are unavailable on this platform are surfaced with
 *    their real error messages (createCameraTextureAsync, getWorkletContext)
 *  - __expoSetLogging / flushEXP / endFrameEXP helpers
 */

import React, {useEffect, useRef, useState} from 'react';
import {
  Image,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import {
  GLView,
  GLLoggingOption,
  type ExpoWebGLRenderingContext,
  type GLSnapshot,
} from 'expo-gl';

type SectionResult = {ok: boolean; text: string} | null;

const VERTEX_SHADER = `
attribute vec2 position;
uniform float angle;
varying vec2 uvPos;
void main() {
  float c = cos(angle);
  float s = sin(angle);
  uvPos = position + vec2(0.5, 0.5);
  gl_Position = vec4(
    position.x * c - position.y * s,
    position.x * s + position.y * c,
    0.0,
    1.0
  );
}
`;

const FRAGMENT_SHADER = `
precision mediump float;
varying vec2 uvPos;
void main() {
  gl_FragColor = vec4(uvPos.x, uvPos.y, 1.0 - uvPos.x, 1.0);
}
`;

function compile(gl: ExpoWebGLRenderingContext, type: number, source: string) {
  const shader = gl.createShader(type)!;
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const info = gl.getShaderInfoLog(shader);
    gl.deleteShader(shader);
    throw new Error('Shader compile failed: ' + info);
  }
  return shader;
}

// One program per context; cached on a plain Map keyed by contextId.
const programCache = new Map<number, WebGLProgram>();

function ensureTriangleProgram(
  gl: ExpoWebGLRenderingContext,
): WebGLProgram {
  const cached = programCache.get(gl.contextId);
  if (cached) {
    return cached;
  }
  const program = gl.createProgram()!;
  gl.attachShader(program, compile(gl, gl.VERTEX_SHADER, VERTEX_SHADER));
  gl.attachShader(program, compile(gl, gl.FRAGMENT_SHADER, FRAGMENT_SHADER));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    throw new Error('Program link failed: ' + gl.getProgramInfoLog(program));
  }
  const buffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
  gl.bufferData(
    gl.ARRAY_BUFFER,
    new Float32Array([0.0, 0.7, -0.7, -0.7, 0.7, -0.7]),
    gl.STATIC_DRAW,
  );
  const position = gl.getAttribLocation(program, 'position');
  gl.enableVertexAttribArray(position);
  gl.vertexAttribPointer(position, 2, gl.FLOAT, false, 0, 0);
  programCache.set(gl.contextId, program);
  return program;
}

function drawTriangleFrame(gl: ExpoWebGLRenderingContext, angle: number) {
  const program = ensureTriangleProgram(gl);
  gl.useProgram(program);
  gl.uniform1f(gl.getUniformLocation(program, 'angle'), angle);
  gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
  gl.clearColor(0.05, 0.05, 0.12, 1.0);
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.drawArrays(gl.TRIANGLES, 0, 3);
  gl.endFrameEXP();
}

function Button({
  title,
  onPress,
}: {
  title: string;
  onPress: () => void;
}) {
  return (
    <Pressable style={styles.button} onPress={onPress}>
      <Text style={styles.buttonText}>{title}</Text>
    </Pressable>
  );
}

function ResultLine({result}: {result: SectionResult}) {
  if (!result) {
    return null;
  }
  return (
    <Text style={result.ok ? styles.resultOk : styles.resultErr}>
      {result.text}
    </Text>
  );
}

function SnapshotPreview({snapshot}: {snapshot: GLSnapshot | null}) {
  if (!snapshot || typeof snapshot.uri !== 'string') {
    return null;
  }
  return (
    <View style={styles.previewRow}>
      <Image source={{uri: snapshot.uri}} style={styles.previewImage} />
      <Text style={styles.previewText}>
        {snapshot.width}x{snapshot.height}
      </Text>
    </View>
  );
}

// ---------------------------------------------------------------------------
// 1. GLView: real-time WebGL rendering + snapshots of the view context
// ---------------------------------------------------------------------------

function GLViewSection() {
  const glRef = useRef<ExpoWebGLRenderingContext | null>(null);
  const rafRef = useRef<number | null>(null);
  const angleRef = useRef(0);
  const [ctxId, setCtxId] = useState<number | null>(null);
  const [animating, setAnimating] = useState(true);
  const [logging, setLogging] = useState(false);
  const [snapshot, setSnapshot] = useState<GLSnapshot | null>(null);
  const [result, setResult] = useState<SectionResult>(null);

  const runLoop = () => {
    rafRef.current = requestAnimationFrame(() => {
      const gl = glRef.current;
      if (gl) {
        if (animating) {
          angleRef.current += 0.06;
          drawTriangleFrame(gl, angleRef.current);
        }
        runLoop();
      }
    });
  };

  useEffect(() => {
    runLoop();
    return () => {
      if (rafRef.current !== null) {
        cancelAnimationFrame(rafRef.current);
      }
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [animating]);

  // Unmount-only: must not live in the [animating] effect above, otherwise
  // toggling pause/resume would drop the GL context for good.
  useEffect(() => {
    return () => {
      programCache.delete(glRef.current?.contextId ?? -1);
      glRef.current = null;
    };
  }, []);

  const takeSnapshot = async (options: {
    format: 'jpeg' | 'png' | 'webp';
    compress?: number;
  }) => {
    try {
      const gl = glRef.current;
      if (!gl) {
        setResult({ok: false, text: '尚未获得 GL 上下文'});
        return;
      }
      const snap = await GLView.takeSnapshotAsync(gl.contextId, {
        format: options.format,
        ...(options.compress !== undefined ? {compress: options.compress} : {}),
      });
      setSnapshot(snap);
      setResult({
        ok: true,
        text: `takeSnapshotAsync(${options.format}${
          options.compress !== undefined ? ', compress=' + options.compress : ''
        }) → ${snap.uri}`,
      });
    } catch (e) {
      setResult({ok: false, text: 'takeSnapshotAsync 失败: ' + String(e)});
    }
  };

  const toggleLogging = () => {
    const gl = glRef.current;
    if (!gl) {
      setResult({ok: false, text: '尚未获得 GL 上下文'});
      return;
    }
    const next = !logging;
    gl.__expoSetLogging(next ? GLLoggingOption.METHOD_CALLS : GLLoggingOption.DISABLED);
    setLogging(next);
    setResult({
      ok: true,
      text: next
        ? '已开启 METHOD_CALLS 日志，后续 GL 调用将打印到 console'
        : '已关闭 GL 日志',
    });
  };

  return (
    <View style={styles.section}>
      <Text style={styles.sectionTitle}>1. GLView 渲染视图</Text>
      <GLView
        style={styles.glView}
        onContextCreate={(gl: ExpoWebGLRenderingContext) => {
          glRef.current = gl;
          setCtxId(gl.contextId);
          drawTriangleFrame(gl, angleRef.current);
        }}
      />
      <Text style={styles.meta}>exglCtxId = {ctxId ?? '等待 surface 创建…'}</Text>
      <View style={styles.buttonRow}>
        <Button
          title={animating ? '暂停动画' : '恢复动画'}
          onPress={() => setAnimating(!animating)}
        />
        <Button
          title="flushEXP"
          onPress={() => {
            const gl = glRef.current;
            if (!gl) {
              return;
            }
            gl.flushEXP();
            setResult({ok: true, text: 'flushEXP 已在 GL 线程同步执行'});
          }}
        />
        <Button title={logging ? '关闭日志' : '开启 GL 日志'} onPress={toggleLogging} />
      </View>
      <View style={styles.buttonRow}>
        <Button title="截图 JPEG (compress 0.8)" onPress={() => takeSnapshot({format: 'jpeg', compress: 0.8})} />
        <Button title="截图 PNG" onPress={() => takeSnapshot({format: 'png'})} />
        <Button title="截图 WebP (降级 PNG)" onPress={() => takeSnapshot({format: 'webp'})} />
      </View>
      <ResultLine result={result} />
      <SnapshotPreview snapshot={snapshot} />
    </View>
  );
}

// ---------------------------------------------------------------------------
// 2. Headless context: createContextAsync + own framebuffer + snapshot + destroy
// ---------------------------------------------------------------------------

const HEADLESS_SIZE = 128;

function HeadlessSection() {
  const glRef = useRef<ExpoWebGLRenderingContext | null>(null);
  const fboRef = useRef<WebGLFramebuffer | null>(null);
  const [busy, setBusy] = useState(false);
  const [snapshot, setSnapshot] = useState<GLSnapshot | null>(null);
  const [result, setResult] = useState<SectionResult>(null);

  const create = async () => {
    try {
      setBusy(true);
      const gl = await GLView.createContextAsync();
      glRef.current = gl;

      // Checkerboard texture content, uploaded directly (exercises
      // texImage2D + typed array bridge on the OHOS native side).
      const size = HEADLESS_SIZE;
      const cell = size / 8;
      const pixels = new Uint8Array(size * size * 4);
      for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
          const on = (Math.floor(x / cell) + Math.floor(y / cell)) % 2 === 0;
          const offset = (y * size + x) * 4;
          pixels[offset] = on ? 236 : 40;
          pixels[offset + 1] = on ? 120 : 60;
          pixels[offset + 2] = on ? 40 : 200;
          pixels[offset + 3] = 255;
        }
      }
      const tex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA,
        size,
        size,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        pixels,
      );
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

      const fbo = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
      gl.framebufferTexture2D(
        gl.FRAMEBUFFER,
        gl.COLOR_ATTACHMENT0,
        gl.TEXTURE_2D,
        tex,
        0,
      );
      gl.viewport(0, 0, size, size);
      fboRef.current = fbo;
      gl.flushEXP();

      setResult({ok: true, text: `createContextAsync → exglCtxId = ${gl.contextId}`});
    } catch (e) {
      setResult({ok: false, text: 'createContextAsync 失败: ' + String(e)});
    } finally {
      setBusy(false);
    }
  };

  const snapshotHeadless = async () => {
    try {
      setBusy(true);
      const gl = glRef.current;
      if (!gl || !fboRef.current) {
        setResult({ok: false, text: '请先创建无头上下文'});
        return;
      }
      const snap = await GLView.takeSnapshotAsync(gl, {
        framebuffer: {id: (fboRef.current as {id: number}).id},
        rect: {x: 0, y: 0, width: HEADLESS_SIZE, height: HEADLESS_SIZE},
        format: 'png',
      });
      setSnapshot(snap);
      setResult({ok: true, text: `takeSnapshotAsync → ${snap.uri}`});
    } catch (e) {
      setResult({ok: false, text: 'takeSnapshotAsync 失败: ' + String(e)});
    } finally {
      setBusy(false);
    }
  };

  const destroy = async () => {
    try {
      setBusy(true);
      const gl = glRef.current;
      if (!gl) {
        setResult({ok: false, text: '没有可销毁的无头上下文'});
        return;
      }
      const destroyed = await GLView.destroyContextAsync(gl);
      glRef.current = null;
      fboRef.current = null;
      setResult({
        ok: destroyed,
        text: `destroyContextAsync → ${destroyed}${destroyed ? '' : '（上下文不存在）'}`,
      });
    } catch (e) {
      setResult({ok: false, text: 'destroyContextAsync 失败: ' + String(e)});
    } finally {
      setBusy(false);
    }
  };

  return (
    <View style={styles.section}>
      <Text style={styles.sectionTitle}>2. 无头上下文 + 帧快照</Text>
      <View style={styles.buttonRow}>
        <Button title="createContextAsync" onPress={create} />
        <Button title="截图 FBO" onPress={snapshotHeadless} />
        <Button title="destroyContextAsync" onPress={destroy} />
      </View>
      <ResultLine result={result} />
      <SnapshotPreview snapshot={snapshot} />
      {busy ? <Text style={styles.meta}>处理中…</Text> : null}
    </View>
  );
}

// ---------------------------------------------------------------------------
// 3. Other public APIs & platform availability
// ---------------------------------------------------------------------------

function CapabilitySection() {
  const [result, setResult] = useState<SectionResult>(null);

  const testCameraTexture = () => {
    // Instance method; exercised on a prototype instance to surface the real
    // availability behavior without needing a camera view.
    const view = Object.create(GLView.prototype) as {
      createCameraTextureAsync(ref: unknown): Promise<{id: number}>;
    };
    view
      .createCameraTextureAsync(null)
      .then(() => setResult({ok: true, text: 'createCameraTextureAsync 意外成功'}))
      .catch((e: Error) =>
        setResult({
          ok: false,
          text: `createCameraTextureAsync 不支持: ${e.message}`,
        }),
      );
  };

  const testGetWorkletContext = () => {
    try {
      const gl = GLView.getWorkletContext(1);
      setResult({
        ok: true,
        text: `getWorkletContext(1) → ${gl ? '获得上下文' : 'undefined'}`,
      });
    } catch (e) {
      setResult({
        ok: false,
        text: `getWorkletContext 不支持: ${(e as Error).message}`,
      });
    }
  };

  const testDestroyObject = () => {
    const view = Object.create(GLView.prototype) as {
      destroyObjectAsync(obj: {id: number}): Promise<boolean>;
    };
    view
      .destroyObjectAsync({id: 424242})
      .then((destroyed) =>
        setResult({
          ok: destroyed,
          text: `destroyObjectAsync({id:424242}) → ${destroyed}（未知对象返回 false，与原库一致）`,
        }),
      )
      .catch((e: Error) => setResult({ok: false, text: 'destroyObjectAsync 失败: ' + e.message}));
  };

  return (
    <View style={styles.section}>
      <Text style={styles.sectionTitle}>3. 其余公开 API / 平台能力</Text>
      <View style={styles.buttonRow}>
        <Button title="createCameraTextureAsync" onPress={testCameraTexture} />
        <Button title="getWorkletContext" onPress={testGetWorkletContext} />
        <Button title="destroyObjectAsync" onPress={testDestroyObject} />
      </View>
      <ResultLine result={result} />
    </View>
  );
}

export default function App() {
  return (
    <ScrollView style={styles.root} contentContainerStyle={styles.content}>
      <Text style={styles.title}>expo-gl · HarmonyOS (RNOH)</Text>
      <GLViewSection />
      <HeadlessSection />
      <CapabilitySection />
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#101318'},
  content: {padding: 16, paddingBottom: 48},
  title: {color: '#FFFFFF', fontSize: 20, fontWeight: '700', marginBottom: 12},
  section: {
    marginBottom: 24,
    padding: 12,
    borderRadius: 10,
    backgroundColor: '#1A1F28',
  },
  sectionTitle: {color: '#9FC1FF', fontSize: 15, fontWeight: '600', marginBottom: 10},
  glView: {width: '100%', height: 220, borderRadius: 8, overflow: 'hidden'},
  meta: {color: '#8B93A3', fontSize: 12, marginTop: 6},
  buttonRow: {flexDirection: 'row', flexWrap: 'wrap', gap: 8, marginTop: 8},
  button: {
    backgroundColor: '#2C67F2',
    borderRadius: 6,
    paddingHorizontal: 10,
    paddingVertical: 8,
    marginRight: 8,
    marginTop: 4,
  },
  buttonText: {color: '#FFFFFF', fontSize: 13},
  resultOk: {color: '#7EE081', fontSize: 12, marginTop: 8},
  resultErr: {color: '#FF9F7F', fontSize: 12, marginTop: 8},
  previewRow: {flexDirection: 'row', alignItems: 'center', marginTop: 8},
  previewImage: {width: 96, height: 96, borderRadius: 6, backgroundColor: '#000'},
  previewText: {color: '#C6CBD6', fontSize: 12, marginLeft: 10},
});
