"""Explicit, local-only Frida debugger for fingerprinted isolated S7 copies.

Use attach_suspended() from an isolated launcher before resuming its owned game.
No launch, registry, profile, archive or game-file modifications occur here.
Only owned copies under work/ are accepted, never an installed original.
"""
import hashlib
import json
import os
from pathlib import Path
import queue
import threading
import time
from profiles import select, PROFILES, STEAM_PACKED, STEAM_TEXT


def decoded_steam_guards(text, addresses):
    rva,size,expected=STEAM_TEXT
    if len(text)!=size or hashlib.sha256(text).hexdigest()!=expected:
        raise ValueError('Decoded Steam text does not match the reviewed image')
    result=[]
    for address in addresses:
        offset=address-0x400000-rva
        if not 0<=offset<=size-32:raise ValueError('Lua API outside decoded Steam text')
        result.append(dict(address=address,bytes=text[offset:offset+32].hex(),relocations=[]))
    return result

LAB_SHA256 = '6dd278274aa885ff402a277f6d2f3ba05600e1ff841462e038f44bf233ce992f'
LAB_HASHES = {'A': LAB_SHA256,
              'B': 'dbc108237301f329fed5372f79915c82acd9085f58d32adefa4efe434208ef72'}
API = dict(newstate=0x458170, close=0x44bf90, sethook=0x445ea0,
           getstack=0x444ce0, getinfo=0x45af70, getlocal=0x44cfc0,
           settop=0x44c570, load=0x4714d0, reader=0x44b3c0, pcall=0x446500)


def validate_target(executable, pid, parent):
    import pefile
    executable = Path(executable).resolve()
    root = Path(__file__).resolve().parents[2]
    if not executable.is_relative_to((root/'work').resolve()):
        raise ValueError('Debugger requires an isolated repository work copy')
    raw = executable.read_bytes()
    pin=hashlib.sha256(raw).hexdigest()
    selected = select(pin)
    slot = selected['slot']
    # Reuse the project's exact native PID/parent/path observer, without caches.
    import sys
    sys.path.insert(0, str(root/'tools/installer'))
    import observe_game
    api = observe_game.Windows()
    rows = [p for p in api.snapshot(2, 0, observe_game.Process, 'Process32FirstW', 'Process32NextW')
            if p.pid == pid]
    if len(rows) != 1 or rows[0].parent != parent or parent != os.getpid():
        raise ValueError('Debugger requires a live game owned by this launcher')
    handle = api.k.OpenProcess(0x1000, False, pid)
    if not handle:
        raise ValueError('Cannot verify owned target path')
    try:
        if api.path(handle) != executable:
            raise ValueError('Live target path mismatch')
    finally:
        api.k.CloseHandle(handle)
    pe = pefile.PE(data=raw)
    if pe.FILE_HEADER.Machine != 0x14c or pe.OPTIONAL_HEADER.ImageBase != 0x400000:
        raise ValueError('Expected x86 preferred-base image')
    relocations = [entry.rva+0x400000 for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', ())
                   for entry in block.entries if entry.type == 3]
    guards = [dict(address=address, bytes=pe.get_data(address-0x400000, 32).hex(),
                   relocations=[dict(offset=r-address, word=int.from_bytes(pe.get_data(r-0x400000, 4), 'little'))
                                for r in relocations if address-3 <= r < address+32])
              for address in selected['api'].values()]
    if pin==STEAM_PACKED:
        import ctypes as C
        from ctypes import wintypes as W
        rva,size,_=STEAM_TEXT
        text=next((s for s in pe.sections if s.Name.rstrip(b'\0')==b'.text'),None)
        if (text is None or text.VirtualAddress!=rva or text.SizeOfRawData!=size
                or pe.OPTIONAL_HEADER.DllCharacteristics&0x40):
            raise ValueError('Unexpected packed Steam code layout')
        reader=C.WinDLL('kernel32',use_last_error=True)
        reader.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];reader.OpenProcess.restype=W.HANDLE
        reader.ReadProcessMemory.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
        reader.ReadProcessMemory.restype=W.BOOL
        reader.CloseHandle.argtypes=[W.HANDLE];reader.CloseHandle.restype=W.BOOL
        handle=reader.OpenProcess(0x1010,False,pid)
        if not handle:raise ValueError('Cannot verify decoded Steam image')
        try:
            if api.path(handle)!=executable:raise ValueError('Live target path mismatch')
            buffer=C.create_string_buffer(size);done=C.c_size_t()
            if not reader.ReadProcessMemory(handle,0x400000+rva,buffer,size,C.byref(done)) or done.value!=size:
                raise ValueError('Cannot read complete decoded Steam text')
            guards=decoded_steam_guards(buffer.raw,selected['api'].values())
        finally:reader.CloseHandle(handle)
    identity = dict(imageSize=pe.OPTIONAL_HEADER.SizeOfImage, timestamp=pe.FILE_HEADER.TimeDateStamp,
                    slot=slot, version=selected['version'])
    pe.close()
    return executable, guards, identity, selected['api']


class Adapter:
    def __init__(self, executable, pid, output, *, selftest=False, pause_ms=10000, breakpoints=(),trace_errors_only=False):
        import frida
        if trace_errors_only and (selftest or breakpoints):
            raise ValueError('Error-only tracing cannot install breakpoints or execute a fixture')
        executable, guards, identity, api = validate_target(executable, pid, os.getpid())
        if not 1000 <= pause_ms <= 30000:
            raise ValueError('Pause deadline must be 1..30 seconds')
        self.output = Path(output)
        if not self.output.resolve().is_relative_to((Path(__file__).resolve().parents[2]/'work').resolve()):
            raise ValueError('Keep debugger observations beneath ignored repository work/')
        self.log = self.output.open('x', encoding='utf-8')
        self.events = queue.Queue()
        self.detached = threading.Event()
        self.session = frida.attach(pid)
        self.session.on('detached', lambda *args: self.detached.set())
        here = Path(__file__).resolve().parent
        config = dict(path=str(executable), api=api, guards=guards, identity=identity, pauseMs=pause_ms,
                      firstGameLine=selftest, breakpoints=list(breakpoints), fixture=None,
                      attachExisting=identity['version'] == 'history-steam',traceErrorsOnly=bool(trace_errors_only))
        if selftest:
            config['fixture'] = (here/'fixture.lua').read_text(encoding='utf-8')
            config['breakpoints'] = [dict(source='weplay/debugger-fixture.lua', line=8)]
        source = 'const CONFIG = '+json.dumps(config)+';\n'
        source += (here/'stepping.js').read_text(encoding='utf-8')+'\n'
        source += (here/'lua_errors.js').read_text(encoding='utf-8')+'\n'
        source += (here/'agent.js').read_text(encoding='utf-8')
        self.script = self.session.create_script(source)
        self.script.on('message', self.message)
        self.closed = False
        try:
            self.script.load()
            deadline = time.monotonic()+10
            while True:
                initial = self.events.get(timeout=max(.01, deadline-time.monotonic()))
                if initial.get('event') == 'ready':
                    break
                if initial.get('event') not in ('local-steam-boundary-observed', 'steam-observer-unavailable',
                                                'steam-startup-result','lua-runtime-error') or time.monotonic() >= deadline:
                    raise RuntimeError('Debugger initialization rejected: '+json.dumps(initial))
        except BaseException:
            self.session.detach()
            self.log.close()
            self.closed = True
            raise

    def message(self, message, data):
        if message['type'] == 'send':
            event = message['payload']
        else:
            event = dict(event='agent-script-error', description=message.get('description'),
                         stack=message.get('stack'))
        self.log.write(json.dumps(event, ensure_ascii=True)+'\n')
        self.log.flush()
        self.events.put(event)

    def command(self, op, **fields):
        self.script.post(dict(type='command', payload=dict(op=op, **fields)))

    def close_after_restored(self, timeout=30):
        if self.closed:
            return
        if self.detached.is_set():
            self.closed = True
            self.log.close()
            return
        self.command('detach')
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline and not self.detached.is_set():
            try:
                event = self.events.get(timeout=.2)
            except queue.Empty:
                continue
            if event['event'] == 'detached':
                self.script.unload()
                self.session.detach()
                self.closed = True
                self.log.close()
                return
        if self.detached.is_set():
            self.closed = True
            self.log.close()
            return
        raise RuntimeError('Hooks have not all restored; retain adapter until owned game exits')

    def selftest(self, timeout=180):
        """Check real VM stops, values and source/depth transitions, not fake events."""
        deadline = time.monotonic()+timeout
        original, fixtures, stage, stops, timeout_seen, rejected = [], 0, 0, [], False, False
        fixture_stacks = 0
        while time.monotonic() < deadline:
            try:
                event = self.events.get(timeout=.2)
            except queue.Empty:
                if self.detached.is_set():
                    raise RuntimeError('Game exited or detached before debugger checks completed')
                continue
            kind = event['event']
            if kind in ('agent-script-error', 'adapter-error', 'restore-failed'):
                raise RuntimeError('Debugger agent failed: '+json.dumps(event))
            if kind == 'command-rejected':
                rejected = True
            if kind == 'paused':
                source, line = event['source'], event['line']
                if source != 'weplay/debugger-fixture.lua':
                    original.append(dict(source=source, line=line, depth=event['depth']))
                    mode = ('stepIn', 'next', 'continue')[min(len(original)-1, 2)]
                    self.command(mode, stop=event['stop'])
                    if len(original) == 3:
                        self.command('fixture')
                    continue
                stops.append(dict(run=fixtures+1, line=line, depth=event['depth'], reason=event['reason']))
                values = {v['name']: v.get('value') for v in event['frames'][0]['locals']}
                if fixtures == 0:
                    expected = ((8, 'stepIn', 'first', 11), (3, 'next', 'value', 11),
                                (4, 'stepOut', 'doubled', 22), (9, 'next', 'second', 22),
                                (10, 'continue', 'result', 25))
                    if stage >= len(expected):
                        raise RuntimeError('Unexpected fixture stop')
                    want, mode, name, value = expected[stage]
                    if line != want or values.get(name) != value:
                        raise RuntimeError('Live step/locals mismatch: '+json.dumps(dict(event=event, expected=expected[stage])))
                    if stage == 0:
                        self.command('continue', stop=event['stop']-1)
                    self.command(mode, stop=event['stop'])
                    stage += 1
                elif fixtures == 1:
                    want, mode = ((8, 'next'), (9, 'continue'))[stage]
                    if line != want:
                        raise RuntimeError('Step-over entered the child function')
                    self.command(mode, stop=event['stop']); stage += 1
                # Third run deliberately exercises the native pause deadline.
            if kind == 'pause-timeout':
                if fixtures != 2:
                    raise RuntimeError('Unexpected pause timeout')
                timeout_seen = True
            if kind == 'fixture-result':
                if event['status'] != 0 or event['answer'] != dict(type='number', value=25):
                    raise RuntimeError('Real game Lua fixture failed')
                if (fixtures == 0 and stage != 5) or (fixtures == 1 and stage != 2):
                    raise RuntimeError('Missing fixture stops')
                fixtures += 1; stage = 0
            if kind == 'fixture-stack':
                if event['balanced'] is not True:
                    raise RuntimeError('Fixture changed the real Lua stack')
                fixture_stacks += 1
                if fixtures < 3:
                    self.command('fixture')
                else:
                    if len(original) != 3 or not timeout_seen or not rejected or fixture_stacks != 3:
                        raise RuntimeError('Incomplete live debugger qualification')
                    # Explicitly validate into, over, and out stack depths.
                    first = [s for s in stops if s['run'] == 1]
                    second = [s for s in stops if s['run'] == 2]
                    if not (first[1]['depth'] > first[0]['depth']
                            and first[2]['depth'] == first[1]['depth']
                            and first[3]['depth'] == first[0]['depth']
                            and second[0]['depth'] == second[1]['depth']):
                        raise RuntimeError('Live stepping depth semantics failed')
                    self.command('breakpoints', items=[])
                    self.close_after_restored()
                    return dict(original_game_stops=original, fixture_stops=stops,
                                fixture_executions=fixtures, lua_stacks_balanced=True,
                                stale_stop_rejected=rejected, pause_timeout_verified=timeout_seen,
                                hooks_restored=True, real_game_lua=True)
        self.command('detach')
        raise TimeoutError('Actual debugger self-test deadline')
