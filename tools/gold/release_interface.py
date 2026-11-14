"""History-led Gold single-player graphics/options composition.

The crossplay laboratory's ordinary menu builder stays byte-identical. Only
the normal installer uses this separately pinned profile from the same original.
"""
import hashlib
import graphics_assets as visual
import menu_interface as menu

GAME_MENU='Script/local/menu/panel/gameMenu.lua'


def game_menu_script(raw):
    _,body,_=menu.profile.decoded_view(raw)
    start='if e["Network"]["IsLoggedIn"]()then'
    end='f,l=o(i,l,"INBOX",t["actions"]["mailbox"])end'
    if body.count(start)!=1 or body.count(end)!=1:
        raise ValueError('Gold retired social menu anchors changed')
    first=body.index(start);last=body.index(end)+len(end)
    if last<=first:raise ValueError('Gold retired social menu order changed')
    return menu.change(raw,body[first:last],'')


def settings_script(raw):
    raw=visual.settings_script(raw)
    # The retired global chat has no notification source. History already omits
    # this checkbox. Keep stored IDs/defaults for old profiles and local callers.
    before=('d=l["feedBackOptionsGroup"]["addItem"]("Chat sound","Sound notification of new chat messages.",'
        'o["gameplayOptions"][i["id"]["chatSound"]])l["gamePlayOptions"][d]={'
        '["flagID"]=i["id"]["chatSound"],["group"]=l["feedBackOptionsGroup"],'
        '["defaultValue"]=o["gameplayOptions"][i["id"]["chatSound"]]}')
    return menu.change(raw,before,'')


def hud_script(raw):
    # Normal Gold currently has no admitted multiplayer service. Keep the
    # original modules/dispatch for the independent experimental pair builder.
    raw=menu.change(raw,'e["fancyDialog"]["addButton"](1,25,300+o,l,"Multiplayer",'
        'i["sessionListCB"],"Play against other Players. Create your own session or enter an already existing one.")','')
    raw=menu.change(raw,'e["fancyDialog"]["setButtonState"](1,"Multiplayer",l)','')
    # Castle Forge needs the menu map; no dead entry on the default black menu.
    before='e["fancyDialog"]["addButton"](2,45,250+l,o,"Castle Forge",i["castleForgeCB"],"Construct your Castle.")'
    raw=menu.change(raw,before,'if e.require("local.tools.boot").mapInMainMenu then '+before+' end;')
    return menu.change(raw,'e["fancyDialog"]["setButtonState"](2,"Castle Forge",l)',
        'if e.require("local.tools.boot").mapInMainMenu then e.fancyDialog.setButtonState(2,"Castle Forge",l) end;')


def build(raw):
    if hashlib.sha256(raw).hexdigest()!=menu.profile.SOURCE:raise ValueError('Gold release interface requires exact original')
    archive=menu.profile.Archive(menu.build(raw))
    changes={visual.CAMERA:visual.camera_script(archive.read(visual.CAMERA)),
             visual.SETTINGS:settings_script(archive.read(visual.SETTINGS)),
             visual.TUTORIAL:visual.tutorial_script(archive.read(visual.TUTORIAL)),
             menu.HUD:hud_script(archive.read(menu.HUD)),
             GAME_MENU:game_menu_script(archive.read(GAME_MENU))}
    result=archive.replace(changes);verified=menu.profile.Archive(result)
    if archive.entries.keys()!=verified.entries.keys():raise ValueError('Gold release interface member set changed')
    for n,e in archive.entries.items():
        if e.values[4]!=256 and verified.read(n)!=changes.get(n,archive.read(n)):
            raise ValueError('Gold release interface verification failed')
    return result
