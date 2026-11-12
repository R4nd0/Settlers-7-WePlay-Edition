'use strict';

// Configuration and S7Stepping are prepended by the guarded Python controller.
// An injected overlay can be misidentified as mainModule in this environment.
const game = Process.getModuleByName('Settlers7R.exe');
const norm = s => s.replace(/\\/g, '/').toLowerCase();
if (Process.arch !== 'ia32' || Process.pointerSize !== 4 || norm(game.path) !== norm(CONFIG.path))
  throw new Error('target-identity-mismatch');
const at = va => game.base.add(va - 0x400000);
const pe = game.base.add(game.base.add(0x3c).readU32());
if (pe.readU32() !== 0x4550 || pe.add(4).readU16() !== 0x14c
    || pe.add(8).readU32() !== CONFIG.identity.timestamp || game.size !== CONFIG.identity.imageSize)
  throw new Error('mapped-pe-identity');
for (const guard of CONFIG.guards) {
  const bytes = Array.from(new Uint8Array(at(guard.address).readByteArray(guard.bytes.length / 2)));
  const expected = guard.bytes.match(/../g).map(x => parseInt(x, 16));
  for (const relocation of guard.relocations) {
    const word = (relocation.word + game.base.sub(ptr(0x400000)).toUInt32()) >>> 0;
    for (let i = 0; i < 4; i++) if (relocation.offset+i >= 0 && relocation.offset+i < expected.length)
      expected[relocation.offset+i] = (word >>> (8*i)) & 255;
  }
  if (bytes.some((b, i) => b !== expected[i]))
    throw new Error('native-code-guard');
}
const native = (name, ret, args) => new NativeFunction(at(CONFIG.api[name]), ret, args,
  { abi: 'mscdecl', scheduling: 'cooperative', exceptions: 'propagate' });
const api = {
  sethook: native('sethook', 'int', ['pointer', 'pointer', 'int', 'int']),
  getstack: native('getstack', 'int', ['pointer', 'int', 'pointer']),
  getinfo: native('getinfo', 'int', ['pointer', 'pointer', 'pointer']),
  getlocal: native('getlocal', 'pointer', ['pointer', 'pointer', 'int']),
  settop: native('settop', 'void', ['pointer', 'int']),
  load: native('load', 'int', ['pointer', 'pointer', 'pointer', 'pointer']),
  pcall: native('pcall', 'int', ['pointer', 'int', 'int', 'int'])
};
const kernel = Process.getModuleByName('kernel32.dll');
const win = (name, ret, args) => new NativeFunction(kernel.getExportByName(name), ret, args,
  { abi: 'stdcall', scheduling: 'cooperative', exceptions: 'propagate' });
const createEvent = win('CreateEventW', 'pointer', ['pointer', 'int', 'int', 'pointer']);
const wait = win('WaitForSingleObject', 'uint', ['pointer', 'uint']);
const signal = win('SetEvent', 'int', ['pointer']);
const close = win('CloseHandle', 'int', ['pointer']);
const wake = createEvent(ptr(0), 0, 0, ptr(0));
if (wake.isNull()) throw new Error('debug-event-unavailable');
const options = Memory.allocUtf8String('Sln');
const states = new Map();
let nextState = 1, nextStop = 1, paused = null, stopping = false, active = 0;
let firstGameLine = !!CONFIG.firstGameLine, fixturePending = false, fixtureRunning = false;
const listeners = [];
let pinned = false;
let runtimeErrors = 0;
if (CONFIG.identity.version === 'history-steam') {
  const exit = Process.getModuleByName('ntdll.dll').getExportByName('RtlExitUserProcess');
  listeners.push(Interceptor.attach(exit, { onEnter(args) {
    const stack = Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 8).map(address => {
      const module = Process.findModuleByAddress(address);
      return module ? { module: module.name, offset: address.sub(module.base).toString() } : { module: 'unmapped' };
    });
    emit('native-exit', { code: args[0].toUInt32(), stack });
  }}));
  listeners.push(Process.attachModuleObserver({ onAdded(module) {
    if (module.name.toLowerCase() !== 'steam_api.dll') return;
    if (module.findExportByName('WePlayLocalSteamBoundary')) {
      emit('local-steam-boundary-observed');
      return;
    }
    for (const name of ['SteamAPI_Init', 'SteamAPI_RestartAppIfNecessary']) {
      const address = module.findExportByName(name);
      if (address) try {
        listeners.push(Interceptor.attach(address, { onLeave(result) {
          emit('steam-startup-result', { api: name, result: (result.toUInt32() & 255) !== 0 });
        }}));
      } catch (_) {
        // Optional read-only diagnostics must not invalidate the Lua hook.
        emit('steam-observer-unavailable', { api: name });
      }
    }
  }}));
}

function emit(event, fields = {}) { send(Object.assign({ event }, fields)); }
function cstring(p, limit = 256) {
  if (p.isNull()) return '';
  let n = 0;
  while (n < limit && p.add(n).readU8() !== 0) n++;
  if (n === limit) throw new Error('debug-text-limit');
  return n ? p.readUtf8String(n) : '';
}
function top(L) { return L.add(0x14).readPointer(); }
function topIndex(L) { return top(L).sub(L.add(0x10).readPointer()).toInt32() / 8; }
function record(L) { return states.get(L.add(8).readPointer().toString()); }
function owner(state) { return Process.getCurrentThreadId() === state.thread; }
function frame(L, ar) {
  if (!api.getinfo(L, options, ar)) throw new Error('lua-getinfo-failed');
  return { source: cstring(ar.add(16).readPointer()), line: ar.add(20).readS32(),
    name: cstring(ar.add(4).readPointer(), 128) };
}
function depth(L) {
  const ar = Memory.alloc(100);
  for (let n = 0; n < 64; n++) if (!api.getstack(L, n, ar)) return n;
  throw new Error('stack-depth-limit');
}
function value(v) {
  const tag = v.add(4).readU32();
  if (tag === 0xffffffff) return { type: 'nil' };
  if (tag === 0xfffffffe || tag === 0xfffffffd) return { type: 'boolean', value: tag === 0xfffffffd };
  if (tag < 0xfffffff3) {
    const number = v.readDouble();
    return { type: 'number', value: Number.isFinite(number) ? number : String(number) };
  }
  // Never serialize game strings, table contents, functions or native pointers.
  return { type: tag === 0xfffffffb ? 'string-redacted' : 'opaque' };
}
function snapshot(L) {
  const before = top(L), index = topIndex(L), frames = [];
  try {
    for (let level = 0; level < 16; level++) {
      const ar = Memory.alloc(100);
      if (!api.getstack(L, level, ar)) break;
      const item = Object.assign({ level, locals: [] }, frame(L, ar));
      if (level < 2) for (let n = 1; n <= 24; n++) {
        const name = api.getlocal(L, ar, n);
        if (name.isNull()) break;
        item.locals.push({ name: cstring(name, 128), ...value(top(L).sub(8)) });
        api.settop(L, index);
      }
      frames.push(item);
    }
  } finally { api.settop(L, index); }
  if (!top(L).equals(before)) throw new Error('lua-stack-unbalanced');
  return frames;
}
function restore(state, L) {
  if (!owner(state)) throw new Error('restore-owner-mismatch');
  const g = L.add(8).readPointer();
  if (!g.add(0xb8).readPointer().equals(hook)) throw new Error('hook-ownership-lost');
  if (!api.sethook(L, state.oldHook, state.oldMask, state.oldCount)) throw new Error('hook-restore-failed');
  states.delete(g.toString());
  emit('state-released', { state: state.id, restored: true });
}
function restoreOwned() {
  for (const state of Array.from(states.values())) if (owner(state)) restore(state, state.L);
}
function hookImpl(L, ar) {
  const state = record(L);
  if (!state) return;
  if (!owner(state)) { state.failed = true; return; }
  active++;
  try {
    if (stopping) { restoreOwned(); return; }
    if (state.failed) { restore(state, L); return; }
    if (ar.readS32() !== 2) return;
    const current = frame(L, ar);
    if (current.line < 1) return;
    const source = S7Stepping.source(current.source);
    if (state.sources.size < 128 && !state.sources.has(source)) {
      state.sources.add(source); emit('source', { state: state.id, source });
    }
    const gameLine = firstGameLine && /(?:^|\/)Script\//i.test(source);
    let reason = gameLine ? 'game-entry' : state.policy.reason(source, current.line, 0);
    // Depth is only required for stepping or a stop, not every ordinary line.
    const needsDepth = gameLine || state.policy.mode !== 'continue' || reason !== null;
    const count = needsDepth ? depth(L) : 0;
    if (!gameLine) reason = state.policy.reason(source, current.line, count);
    if (!reason) return;
    firstGameLine = false;
    const id = nextStop++;
    const stop = { id, state, command: null, depth: count };
    paused = stop;
    emit('paused', { stop: id, state: state.id, reason, depth: count,
      source, line: current.line, thread: state.thread, frames: snapshot(L) });
    // Cooperative native wait allows the Frida message thread to post a command.
    // Lua APIs are called only here, on the original game owner thread.
    const status = wait(wake, CONFIG.pauseMs);
    if (status !== 0 || !stop.command) {
      state.policy.resume('continue', count); stopping = true;
      emit('pause-timeout', { stop: id });
    } else state.policy.resume(stop.command, count);
    paused = null;
    emit('resumed', { stop: id, mode: state.policy.mode });
    if (stopping) restoreOwned();
  } catch (error) {
    state.failed = true; emit('adapter-error', { category: 'owner-hook-failure', detail: String(error).slice(0, 256), stack: error.stack });
    // Do not unwind a JS exception through LuaJIT's native VM.
    if (owner(state)) try { restore(state, L); } catch (_) { emit('restore-failed'); }
  } finally { active--; }
}
const hook = new NativeCallback(hookImpl, 'void', ['pointer', 'pointer'], 'mscdecl');

function registerState(L, thread) {
  const g = L.add(8).readPointer(), key = g.toString();
  if (states.has(key)) return;
  if (states.size >= 8) throw new Error('state-registration-limit');
  const main = g.add(0x84).readPointer();
  if (main.isNull() || !main.add(8).readPointer().equals(g)) throw new Error('main-state-ownership');
  const oldHook = g.add(0xb8).readPointer(), oldMask = g.add(0x81).readU8() & 15;
  if (!oldHook.isNull() || oldMask !== 0) throw new Error('existing-debug-hook');
  const policy = new S7Stepping(); policy.setBreakpoints(CONFIG.breakpoints);
  const state = { id: nextState++, L: ptr(main.toString()), thread, oldHook, oldMask,
    oldCount: g.add(0xb4).readS32(), policy, sources: new Set(), failed: false };
  states.set(key, state);
  // Only raw Lua hook ownership requires pinning. Initialization failures before
  // the first hook must remain unloadable without waiting for the game to exit.
  if (!pinned) { Script.pin(); pinned = true; }
  if (!api.sethook(L, hook, 4, 0)) throw new Error('hook-install-failed');
  emit('state-added', { state: state.id, thread: state.thread });
}

function commands() {
  recv('command', message => {
    try {
      const c = message.payload;
      if (CONFIG.traceErrorsOnly && c.op !== 'detach') throw new Error('error-trace-has-no-execution-commands');
      if (c.op === 'detach') {
        stopping = true;
        if (paused) { paused.command = 'continue'; signal(wake); }
      } else if (c.op === 'fixture' && CONFIG.fixture) {
        fixturePending = true;
      } else if (c.op === 'breakpoints') {
        for (const state of states.values()) state.policy.setBreakpoints(c.items);
        CONFIG.breakpoints = c.items;
      } else if (['continue', 'stepIn', 'next', 'stepOut'].includes(c.op)) {
        if (!paused || c.stop !== paused.id || paused.command !== null) throw new Error('stale-stop');
        paused.command = c.op; signal(wake);
      } else throw new Error('invalid-command');
    } catch (_) { emit('command-rejected'); }
    commands();
  });
}
commands();
listeners.push(Interceptor.attach(at(CONFIG.api.newstate), {
  onLeave(L) {
    if (CONFIG.traceErrorsOnly || stopping || L.isNull()) return;
    registerState(L, this.threadId);
  }
}));
listeners.push(Interceptor.attach(at(CONFIG.api.close), {
  onEnter(args) {
    const state = record(args[0]);
    if (state && owner(state)) restore(state, args[0]);
  }
}));
listeners.push(Interceptor.attach(at(CONFIG.api.pcall), {
  onEnter(args) {
    this.L = ptr(args[0].toString());
    if (!CONFIG.traceErrorsOnly && CONFIG.attachExisting && !stopping && !record(this.L)) registerState(this.L, this.threadId);
  },
  onLeave(result) {
    if (!stopping && result.toInt32() !== 0 && runtimeErrors < 128) {
      runtimeErrors++;
      let location = null;
      try {
        const v = top(this.L).sub(8);
        // Reviewed x86 LuaJIT GCstr: len at 12, payload at 16. Read only
        // protected-call errors; the log retains only the strict parser result.
        if (v.add(4).readU32() === 0xfffffffb) {
          const s = v.readPointer(), length = s.add(12).readU32();
          if (length > 0 && length <= 512) location = S7LuaError(s.add(16).readUtf8String(length));
        }
      } catch (_) { /* Opaque errors remain redacted. */ }
      emit('lua-runtime-error', { status: result.toInt32(), location });
    }
    if (!fixturePending || fixtureRunning || stopping || result.toInt32() !== 0) return;
    const L = this.L, state = record(L);
    if (!state || !owner(state) || active || paused) return;
    fixturePending = false; fixtureRunning = true;
    const before = top(L), index = topIndex(L);
    try {
      const text = Memory.allocUtf8String(CONFIG.fixture), context = Memory.alloc(8);
      context.writePointer(text); context.add(4).writeU32(CONFIG.fixture.length);
      const name = Memory.allocUtf8String('@weplay/debugger-fixture.lua');
      const loaded = api.load(L, at(CONFIG.api.reader), context, name);
      const status = loaded === 0 ? api.pcall(L, 0, 1, 0) : loaded;
      const answer = status === 0 ? value(top(L).sub(8)) : null;
      emit('fixture-result', { state: state.id, status, answer, originalGameState: true });
    } finally {
      api.settop(L, index); fixtureRunning = false;
      emit('fixture-stack', { balanced: top(L).equals(before) });
    }
  }
}));
const cleanup = setInterval(() => {
  if (!stopping || states.size !== 0 || active !== 0 || paused || fixtureRunning) return;
  for (const listener of listeners) listener.detach();
  clearInterval(cleanup); close(wake); emit('detached', { hooksRestored: true });
  if (pinned) { pinned = false; Script.unpin(); }
}, 50);
emit('ready', { profile: CONFIG.identity.version, slot: CONFIG.identity.slot, nativeHooks: true });
