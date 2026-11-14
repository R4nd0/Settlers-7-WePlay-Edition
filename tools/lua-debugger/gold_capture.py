"""Opt-in, exact-marker observation of reviewed Gold Lua UI callbacks."""
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import threading
import time

import debugger

PREFIX = 'WEPLAY_GOLD_CAPTURE_V1 '
TEST_MARKERS = (PREFIX+'console-test', PREFIX+'log-test', PREFIX+'complete')
CALLBACKS = dict(log=0x984210, console=0x9842a0)
# Complete original callback bodies, binding rows, names and check-string helper.
# Digests describe preferred-base file bytes, not relocated process memory.
REGIONS = (
    (0x984210,65,'7170db98679dc6a4cda53f2073bddc1a0fed2ffe959cdc07f05a61ea46604bb0'),
    (0x9842a0,65,'0707b1780ae9481c57ab475b551f47343ab985fedafa8e28286c7e922234fdf0'),
    (0x10abdd8,16,'085a4f722723b9b4f3295388220f5ac95fac96ea4109178317e20ffaae1ddc1c'),
    (0x10abde8,16,'dcc87a8f5ee5166efcff3ae11b01fcaf5763edbf0baf74bc815028da8fc7f0e7'),
    (0xf4d2e0,11,'71c69675ca3475d31ea13f756fbd49a6da7c75fb307065e297ca7b82f3f17547'),
    (0xf4d2ec,15,'a1466bb7a12c2514436251d62a89305fbc39b88e9141d60ddd1e6b2522ffdd6d'),
    (0xbe4c90,96,'93c449c1f2aa7377f2745b28d30e350d4e2148066c8227e2df35d7a72e2232f2'),
)


def markers(values):
    values=list(values)
    if not 1<=len(values)<=16 or any(not isinstance(v,str) for v in values) or len(set(values))!=len(values):
        raise ValueError('Gold capture requires 1..16 distinct explicit markers')
    if any(not isinstance(v,str) or not v.startswith(PREFIX) or not len(PREFIX)<len(v)<=128
           or re.fullmatch(r'[A-Za-z0-9_ .:=+-]+',v) is None for v in values):
        raise ValueError('Invalid bounded Gold diagnostic marker')
    return values


def channels(values):
    values=tuple(values)
    if not values or len(set(values))!=len(values) or any(v not in CALLBACKS for v in values):
        raise ValueError('Select original Gold log and/or console callbacks')
    return values


def guards(pe,selected=('log','console')):
    selected=channels(selected)
    omitted=set()
    if 'log' not in selected:omitted.update((0x984210,0x10abdd8,0xf4d2e0))
    if 'console' not in selected:omitted.update((0x9842a0,0x10abde8,0xf4d2ec))
    relocations=[entry.rva+0x400000 for block in getattr(pe,'DIRECTORY_ENTRY_BASERELOC',())
                 for entry in block.entries if entry.type==3]
    result=[]
    for address,size,expected in REGIONS:
        if address in omitted:continue
        value=pe.get_data(address-0x400000,size)
        if len(value)!=size or hashlib.sha256(value).hexdigest()!=expected:
            raise ValueError('Gold console callback/binding guard mismatch')
        result.append(dict(address=address,bytes=value.hex(),relocations=[
            dict(offset=r-address,word=int.from_bytes(pe.get_data(r-0x400000,4),'little'))
            for r in relocations if address-3<=r<address+size]))
    return result


class GoldCaptureAdapter(debugger.Adapter):
    """Separate observer agent: no Lua line hooks or execution commands."""
    def __init__(self,executable,pid,output,*,allowed,selftest=False,selected=('log','console')):
        allowed=markers(allowed)
        selected=channels(selected)
        if selftest and set(selected)!=set(CALLBACKS):raise ValueError('The Gold marker self-test checks both callbacks')
        if selftest and allowed!=list(TEST_MARKERS):raise ValueError('Use the fixed Gold marker fixture')
        executable,code,identity,api=debugger.validate_target(executable,pid,os.getpid())
        if identity['version']!='gold':raise ValueError('Gold marker capture requires the reviewed Gold image')
        import pefile
        import frida
        pe=pefile.PE(str(executable))
        try:code+=guards(pe,selected)
        finally:pe.close()
        self.output=Path(output)
        if not self.output.resolve().is_relative_to((Path(__file__).resolve().parents[2]/'work').resolve()):
            raise ValueError('Keep capture output under repository work/')
        self.log=self.output.open('x',encoding='utf-8')
        self.events=queue.Queue();self.detached=threading.Event();self.closed=False
        self.session=frida.attach(pid);self.session.on('detached',lambda *args:self.detached.set())
        here=Path(__file__).parent
        config=dict(path=str(executable),identity=identity,api=api,guards=code,
                    callbacks={k:v for k,v in CALLBACKS.items() if k in selected},markers=allowed)
        source='const CONFIG = '+json.dumps(config)+';\n'+(here/'gold_capture_policy.js').read_text()+'\n'+(here/'gold_capture_agent.js').read_text()
        try:
            self.script=self.session.create_script(source);self.script.on('message',self.message);self.script.load()
            event=self.events.get(timeout=15)
            if event.get('event')!='ready':raise RuntimeError('Gold capture initialization failed')
        except BaseException:
            self.session.detach();self.log.close();self.closed=True;raise

    def selftest(self,timeout=120):
        deadline=time.monotonic()+timeout;seen=[]
        while time.monotonic()<deadline:
            if self.detached.is_set():raise RuntimeError('Game exited before Gold marker qualification')
            try:event=self.events.get(timeout=.2)
            except queue.Empty:continue
            kind=event.get('event')
            if kind in ('agent-script-error','capture-failed'):raise RuntimeError('Gold capture failed')
            if kind=='gold-lua-marker':
                if not event.get('lua_stack_unchanged') or not event.get('nonvolatile_registers_preserved') or event.get('original_return')!=0:
                    raise RuntimeError('Original Gold callback ABI changed')
                seen.append((event['channel'],event['marker']))
            if kind=='gold-lua-marker' and event['marker']==2:
                if seen!=[('console',0),('log',1),('console',2)]:raise RuntimeError('Unexpected/missing Gold marker records')
                self.close_after_restored()
                return dict(real_game_lua=True,channels=['console','log'],captured_markers=3,
                    negative_markers_excluded=True,lua_stacks_balanced=True,original_returns_preserved=True,
                    nonvolatile_registers_preserved=True,hooks_restored=True,lua_line_hooks_installed=False)
        self.command('detach');raise TimeoutError('Gold marker qualification deadline')
