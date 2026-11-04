import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(ROOT/'work/lua-test-runtime')]
import ingame_invitation as bridge
import json
from join_crypto import encoded
PUBLIC='weplay-directory1.'+encoded(json.dumps(dict(v=1,origin='https://weplay-multiplayer.weplay-community.workers.dev')).encode('ascii'))
try:
    from lupa.lua51 import LuaRuntime
except ImportError:LuaRuntime=None


class BridgeTests(unittest.TestCase):
    def test_game_modes_never_return_private_room_credentials(self):
        self.assertEqual(bridge.configuration(None,None,None),('unavailable',None))
        self.assertEqual(bridge.configuration({},None,None),('lan',None))
        self.assertEqual(bridge.configuration({},dict(invitation='weplay1.synthetic-secret'),None),('guest',None))
        self.assertEqual(bridge.configuration({},dict(invitation=None),None),('private',None))
        with patch.object(bridge.game_directory,'configured',return_value='https://weplay-multiplayer.weplay-community.workers.dev'):
            mode,code=bridge.configuration({},dict(invitation=None,public_lobby=True),None)
            self.assertEqual(mode,'public');self.assertEqual(bridge.game_directory.parse_code(code)[1],None)
        with self.assertRaises(ValueError):bridge.Bridge(ROOT/'work','public',lambda **value:None,public_code='weplay1.secret')
    def test_current_run_host_only_rate_limited_and_cleaned(self):
        base=Path(os.environ.get('S7_TEST_TEMP_ROOT',ROOT/'work/test-temp'));base.mkdir(parents=True,exist_ok=True)
        for mode in ('private','public','lan','guest','unavailable'):
            with self.subTest(mode=mode),tempfile.TemporaryDirectory(dir=base) as temporary:
                calls=[];now=[0]
                instance=bridge.Bridge(temporary,mode,lambda **value:calls.append(value),
                    public_code=PUBLIC if mode=='public' else None,clock=lambda:now[0]).start()
                # Deterministic polling, without the asynchronous dispatcher.
                instance.stop.set();instance.thread.join();instance.stop.clear()
                data=Path(temporary)/'LocalData'
                raw=(data/bridge.SNAPSHOT).read_text()
                self.assertEqual(raw,bridge.HEADER+instance.nonce+'|'+mode+'\n')
                for token in ('z'*32,'a'*500,instance.nonce,instance.nonce):
                    (data/bridge.REQUEST).write_text(token);instance.poll()
                expected=1 if mode in ('private','public') else 0
                self.assertEqual(len(calls),expected)
                if expected:
                    self.assertEqual(calls[0]['run'],instance.nonce)
                    self.assertEqual(set(calls[0]),{'run','mode'}|({'code'} if mode=='public' else set()))
                now[0]=3;(data/bridge.REQUEST).write_text(instance.nonce);instance.poll()
                self.assertEqual(len(calls),expected*2)
                instance.close();self.assertFalse((data/bridge.SNAPSHOT).exists());self.assertFalse((data/bridge.REQUEST).exists())
                # A request written after shutdown is never dispatched.
                (data/bridge.REQUEST).write_text(instance.nonce);instance.poll()
                self.assertEqual(len(calls),expected*2)

    def test_public_without_directory_never_emits_room_code(self):
        base=ROOT/'work/test-temp';base.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as temporary:
            calls=[];instance=bridge.Bridge(temporary,'public',lambda **value:calls.append(value)).start()
            (instance.data/bridge.REQUEST).write_text(instance.nonce);instance.poll()
            instance.close();self.assertEqual(calls,[])


@unittest.skipIf(LuaRuntime is None,'Optional private Lua 5.1 runtime')
class LuaTests(unittest.TestCase):
    def test_all_modes_and_malformed_fallback_never_call_legacy_overlay(self):
        source=(ROOT/'tools/game-directory/host_invitation.lua').read_text(encoding='utf-8')
        for mode in ('private','public','lan','guest','unknown','missing','malformed'):
            with self.subTest(mode=mode):
                lua=LuaRuntime(unpack_returned_tuples=True)
                context=lua.execute('''
local C={messages=0,requests=0,accept=true}
C.G={UI={WriteToConsole=function(command)
if command=="weplay-invitation-read-v1" then return C.raw end C.command=command;C.requests=C.requests+1;return C.accept end},
Network={ShowInviteOverlay=function()error("Legacy overlay must never be called")end},
ui={messageBox=function(title,text)C.messages=C.messages+1;C.text=text end}}
return C
''')
                context.raw=None if mode=='missing' else ('return os.execute("never")' if mode=='malformed' else bridge.HEADER+'a'*32+'|'+mode+'\n')
                action=lua.execute(source,context.G);action()
                self.assertEqual(context.requests,1 if mode in ('private','public') else 0)
                self.assertEqual(context.messages,0 if mode in ('private','public') else 1)
                if context.requests:
                    self.assertEqual(context.command,'weplay-invitation-open-v1:'+'a'*32)
                    context.accept=False;action();self.assertEqual(context.messages,1)

if __name__=='__main__':unittest.main()
