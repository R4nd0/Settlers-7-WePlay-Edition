"""Execute the fingerprinted Gold loading script with a Lua 5.1 UI model.

Requires the private source script and lupa.lua51. The original closing event
and progress callbacks execute; no real game, network or simulation is started.
"""
from pathlib import Path
import argparse
import json
import loading_screen
from lupa.lua51 import LuaRuntime, LuaError

BACKEND=r'''
local C={destroyed={},nextID=100,candleCreates=0,candleDestroys=0,progress=nil}
Assert=assert
require=function()return {destroy=function()end,currentCommandLineMap=function()return nil end}end
Game={LoadScript=function()end,StartGDState=function()error('Unexpected world start')end}
color={messageDark='',white='',messageLight=''}
defaultPriorities={loadingScreen=9}
desktop={overlayFrame=1}
frame={create=function()C.nextID=C.nextID+1;return C.nextID end,
 destroy=function(id)assert(id);C.destroyed[#C.destroyed+1]=id end}
mouse={cursor={default=1,hourglass=2},visual={set=function(value)C.cursor=value end}}
UI={Anchor={Center=1,Left=2,Top=3,Right=4,Bottom=5},GetTextFromLoca=function(s)return s end}
font={small=1}
image={create=function()return 200 end}
ui={setSize=function()end,setPosition=function()end,setPriority=function()end}
label={create=function()return 201 end,setContent=function()end}
Sound={MuteIngameSounds=function()end}
system={getScreenSize=function()return 1920,1080 end,
 setUpdateFunctions=function(callback)C.engineUpdate=callback end}
C.installCandle=function(created)
 C.candleCreated=created
 candleProgress={
  create=function()assert(not C.candleCreated);C.candleCreated=true;C.candleCreates=C.candleCreates+1 end,
  isCreated=function()return C.candleCreated end,
  destroy=function()assert(C.candleCreated,'Duplicate candle destruction');C.candleCreated=false;C.candleDestroys=C.candleDestroys+1 end,
  setProgress=function(value)assert(C.candleCreated);C.progress=value end}
end
return C
'''


def model(source,candle=None):
    lua=LuaRuntime(unpack_returned_tuples=True)
    state=lua.execute(BACKEND)
    if candle is not None:state.installCandle(candle)
    lua.execute(source.decode('utf-8-sig'))
    return lua,state,lua.globals().loadingScreen


def verify(raw):
    patched=loading_screen.patch(raw)
    checks=0
    # The original fails before reaching frame.destroy when the optional module
    # is missing. This reproduces the observed persistent overlay in the model.
    lua,state,page=model(raw)
    page.frame=41
    for callback in (lambda:page.updateOnLoadProgress(100),page.update):
        try:callback()
        except LuaError as error:
            if 'nil value' not in str(error):raise
        else:raise AssertionError('Original missing-candle failure did not reproduce')
        if page.frame!=41 or len(state.destroyed)!=0:raise AssertionError('Original failed after closing')
        checks+=1
    # The original also destroys an existing candle twice.
    lua,state,page=model(raw,True)
    page.frame=41
    try:page.update()
    except LuaError as error:
        if 'Duplicate candle destruction' not in str(error):raise
    else:raise AssertionError('Original duplicate destruction did not reproduce')
    if state.candleDestroys!=1 or page.frame!=41:raise AssertionError('Wrong original failure phase')
    checks+=1
    for candle in (None,False,True):
        lua,state,page=model(patched,candle)
        page.frame=41
        lua.globals().loadBar.frame=42
        lua.globals().loadBar.currentWidth=790
        page.updateOnLoadProgress(100)
        if page.frame!=41 or len(state.destroyed)!=0:raise AssertionError('Progress prematurely closed UI')
        if candle is True and state.progress!=1:raise AssertionError('Existing candle progress was lost')
        if candle is None and lua.globals().candleProgress is not None:raise AssertionError('Candle was synthesized')
        checks+=1
        page.close()
        if page.frame!=41 or page.closeHack!=30:raise AssertionError('Original close request changed')
        checks+=1
        page.update()
        if page.frame is not None or lua.globals().loadBar.frame is not None:
            raise AssertionError('Original closing callback left UI open')
        if [state.destroyed[i] for i in range(1,len(state.destroyed)+1)]!=[42,41]:
            raise AssertionError('Original frames were not destroyed exactly once')
        if state.candleDestroys!=(1 if candle is True else 0):raise AssertionError('Duplicate candle destroy')
        page.update()
        if len(state.destroyed)!=2:raise AssertionError('Repeated close destroyed more frames')
        checks+=1
    for candle in (None,False):
        lua,state,page=model(patched,candle)
        page.create(page.creationMode.backgroundMap)
        if not page.isCreated() or state.engineUpdate is None:raise AssertionError('Original create/update registration lost')
        if candle is False and state.candleCreates!=1:raise AssertionError('Existing candle create was lost')
        page.updateOnLoadProgress(100)
        if not page.isCreated():raise AssertionError('Progress bypassed original closing event')
        page.update()
        if page.isCreated():raise AssertionError('Background UI did not close')
        checks+=1
    try:loading_screen.patch(raw+b' ')
    except ValueError:checks+=1
    else:raise AssertionError('Unknown script was accepted')
    return {'lua51_lifecycle_checks':checks,'original_missing_module_and_double_destroy_reproduced':True,
            'patched_original_callbacks_passed':True,'progress_does_not_force_close':True,
            'original_world_start_and_network_state_unchanged':True,'game_started':False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--script',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    output=args.output.resolve()
    if not output.is_relative_to((root/'work').resolve()) or output.exists():
        parser.error('Use a new private output under work/')
    result=verify(args.script.read_bytes())
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,indent=2))
    print(json.dumps(result))


if __name__=='__main__':main()
