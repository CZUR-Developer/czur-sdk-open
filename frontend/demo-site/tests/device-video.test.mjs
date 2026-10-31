import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import test from 'node:test';
import { transformWithEsbuild } from 'vite';

const serviceUrl = new URL('../src/services/device-video.ts', import.meta.url);
const { code } = await transformWithEsbuild(await readFile(serviceUrl, 'utf8'), serviceUrl.pathname, {
  loader: 'ts', format: 'cjs',
});

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

async function settle() {
  await new Promise((resolve) => setImmediate(resolve));
}

async function createPreview() {
  const sockets = [];
  const animationFrames = [];
  const imageDecodes = [];
  let stream = 0;
  class Socket {
    static CLOSING = 2;
    readyState = 1;
    listeners = new Map();
    constructor() { sockets.push(this); }
    addEventListener(type, listener) { this.listeners.set(type, listener); }
    emit(data) { this.listeners.get('message')?.({ data }); }
    close() { this.readyState = 3; }
  }
  const resolution = { width: 2, height: 1, fps: 15, pixel_format: 'mjpeg', is_default: true };
  const module = { exports: {} };
  const imports = {
    vue: { reactive: (value) => value, readonly: (value) => value },
    './auth-session': {
      sendBoundCommand: async (method) => ({ code: 0, data: method === 'video.start'
        ? { stream_id: `stream-${++stream}`, session_token: 'test-token' }
        : { opened: true, resolutions: [resolution] } }),
    },
    './protocol': { buildVideoWsUrl: () => 'ws://test/video', isOkResponse: (value) => value.code === 0 },
    './runtime-records': {
      nowTimeLabel: () => 'now', recordAlert() {}, recordInternalRuntimeEvent() {}, recordRuntimeEvent() {},
    },
  };
  vm.runInNewContext(code, {
    module, exports: module.exports, Blob, ArrayBuffer, Uint8Array, WebSocket: Socket,
    require: (name) => {
      assert.ok(imports[name], `unexpected import: ${name}`);
      return imports[name];
    },
    requestAnimationFrame: (callback) => animationFrames.push(callback),
    window: { createImageBitmap: true },
    createImageBitmap: () => {
      const decode = deferred();
      imageDecodes.push(decode);
      return decode.promise;
    },
  }, { filename: serviceUrl.pathname });
  const api = module.exports;
  const draws = [];
  api.attachVideoCanvas({ width: 2, height: 1, getContext: () => ({
    clearRect() {}, drawImage: (image) => draws.push(image.id),
  }) });
  await api.selectDevice('device-1');
  await api.openSelectedDevice();
  await api.startVideo();
  const meta = (seq, overrides = {}) => ({
    stream_id: api.deviceVideoState.streamId, frame_seq: seq,
    width: 2, height: 1, pixel_format: 'mjpeg', ...overrides,
  });
  const sendMeta = (value, socket = sockets.at(-1)) => socket.emit(JSON.stringify({
    event: 'stream.frame_meta', payload: value,
  }));
  return {
    api, sockets, animationFrames, imageDecodes, draws, meta, sendMeta,
    state: api.deviceVideoState,
    sendFrame: (value, binary = new ArrayBuffer(2)) => {
      sendMeta(value);
      sockets.at(-1).emit(binary);
    },
    runAnimation: () => {
      const callback = animationFrames.shift();
      assert.ok(callback, 'a frame should be scheduled');
      callback();
    },
    finishDecode: async (index, id) => {
      imageDecodes[index].resolve({ id, width: 2, height: 1, close() {} });
      await settle();
    },
  };
}

class DelayedBlob extends Blob {
  conversion = deferred();
  arrayBuffer() { return this.conversion.promise; }
}

test('ArrayBuffer metadata stays paired when an earlier decode finishes between metadata and binary', async () => {
  const preview = await createPreview();
  preview.sendFrame(preview.meta(1));
  preview.runAnimation();
  preview.sendMeta(preview.meta(2));
  await preview.finishDecode(0, 'first');
  // The UI now describes rendered frame 1, while frame 2 is arriving.
  assert.equal(preview.state.frameMeta.frame_seq, 1);
  preview.sockets.at(-1).emit(new ArrayBuffer(2));
  preview.runAnimation();
  await preview.finishDecode(1, 'second');
  assert.equal(preview.state.lastRenderedSeq, 2);
  assert.equal(preview.state.renderedFrameCount, 2);
  assert.equal(preview.state.decodeFailedCount, 0);
  assert.deepEqual(preview.draws, ['first', 'second']);
});

test('out-of-order Blob conversion cannot replace a newer pending frame', async () => {
  const preview = await createPreview();
  const first = new DelayedBlob();
  const second = new DelayedBlob();
  preview.sendFrame(preview.meta(1), first);
  preview.sendFrame(preview.meta(2), second);
  preview.sendFrame(preview.meta(3));
  preview.sendFrame(preview.meta(4));
  second.conversion.resolve(new ArrayBuffer(2));
  first.conversion.resolve(new ArrayBuffer(2));
  await settle();
  preview.runAnimation();
  // Frame 3 yields to pending frame 4, which older conversions cannot replace.
  await preview.finishDecode(0, 'fourth');
  assert.equal(preview.state.lastRenderedSeq, 4);
  assert.equal(preview.state.renderedFrameCount, 1);
  assert.equal(preview.animationFrames.length, 0);
});

test('a busy animation loop draws the latest frame instead of repeatedly postponing decoding', async () => {
  const preview = await createPreview();
  for (let frame = 1; frame <= 3; frame++) {
    preview.sendFrame(preview.meta(frame * 2 - 1));
    // A faster producer delivers another frame before each animation callback.
    preview.sendFrame(preview.meta(frame * 2));
    preview.runAnimation();
    assert.equal(preview.imageDecodes.length, frame, 'every animation callback should begin decoding');
    await preview.finishDecode(frame - 1, `frame-${frame * 2}`);
  }
  assert.equal(preview.state.lastRenderedSeq, 6);
  assert.equal(preview.state.renderedFrameCount, 3);
  assert.equal(preview.state.decodeFailedCount, 0);
  assert.deepEqual(preview.draws, ['frame-2', 'frame-4', 'frame-6']);
});

test('Blob conversion finishing after reset is ignored, even if metadata was missing', async () => {
  const preview = await createPreview();
  const oldBlob = new DelayedBlob();
  preview.sockets.at(-1).emit(oldBlob);
  await preview.api.stopVideo();
  await preview.api.startVideo();
  oldBlob.conversion.resolve(new ArrayBuffer(2));
  await settle();
  assert.equal(preview.state.receivedFrameCount, 0);
  assert.equal(preview.animationFrames.length, 0);
});

test('reset clears queued metadata and ignores messages from a closed socket', async () => {
  const preview = await createPreview();
  const oldSocket = preview.sockets.at(-1);
  const oldMeta = preview.meta(100, { width: 999 });
  preview.sendMeta(oldMeta);
  await preview.api.stopVideo();
  await preview.api.startVideo();
  preview.sendMeta(oldMeta, oldSocket);
  oldSocket.emit(new ArrayBuffer(2));
  preview.sendFrame(preview.meta(1));
  preview.runAnimation();
  await preview.finishDecode(0, 'new-stream');
  assert.equal(preview.state.receivedFrameCount, 1);
  assert.equal(preview.state.lastRenderedSeq, 1);
  assert.equal(preview.state.frameMeta.width, 2);
});

test('Blob conversion failure is counted without blocking the next frame', async () => {
  const preview = await createPreview();
  const broken = new DelayedBlob();
  preview.sendFrame(preview.meta(1), broken);
  broken.conversion.reject(new Error('conversion failed'));
  await settle();
  preview.sendFrame(preview.meta(2));
  preview.runAnimation();
  await preview.finishDecode(0, 'second');
  assert.equal(preview.state.lastRenderedSeq, 2);
  assert.equal(preview.state.decodeFailedCount, 1);
  assert.equal(preview.state.receivedFrameCount, 2);
});
