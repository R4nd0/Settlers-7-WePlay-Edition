-- Original WePlay global browser. Public data is parsed as data, never loaded
-- as a script. A room switch closes this menu-only game normally before launch.
local G, Panel, State = ...
local Strings = {title="WePlay Online",refresh="Aktualisieren / Refresh",join="Beitreten / Join",
    empty="Keine offenen Partien / No open games",unavailable="Spieleverzeichnis im Launcher verbinden. / Connect the directory in the launcher.",
    confirm="WePlay startet das Spiel im Raum des Hosts neu. Danach die LAN-Partie waehlen. Fortfahren? / WePlay will restart the game in the host's room. Continue?"}
local List = G.require("local.ui.listControl").listControl
local Confirm = G.require("local.ui.confirmDialog")
local create, update, destroy = Panel.create, Panel.update, Panel.destroy
local frame, list, notice, joinButton, confirmation, nonce, revision, lastChange, lastRead
local rows = {}
local function nativeVisible(visible)
    local action=visible and G.ui.show or G.ui.hide
    if State.sessionsList then action(State.sessionsList.getMainFrame()) end
    for _,control in _G.ipairs({State.quickMatchButtonID,State.createSessionButtonID,State.joinSessionButtonID}) do action(control) end
end
local function bridge(command) return G.UI.WriteToConsole(command) end
local function hex(value)
    if #value % 2 ~= 0 or value:find("[^0-9a-f]") then return nil end
    return (value:gsub("..",function(pair) return _G.string.char(_G.tonumber(pair,16)) end))
end
local function validID(value) return _G.type(value)=="string" and #value==32 and not value:find("[^0-9a-f]") end
local function read()
    local raw = bridge("weplay-directory-read-v1")
    if _G.type(raw)~="string" or #raw>32768 then return false end
    local current, serial, body=raw:match("^weplay%-games%-ui%-v1|([0-9a-f]+)|(%d+)\n(.*)$")
    if not validID(current) or not serial or #serial>10 then return false end
    if current~=nonce or serial~=revision then nonce,revision,lastChange=current,serial,G.Game.GetRunTime() end
    if G.Game.GetRunTime()-lastChange>35000 then return false end
    local parsed={}
    for line in body:gmatch("[^\n]+") do
        local id,state,players,capacity,title=line:match("^([0-9a-f]+)|([ofsi])|([1-4])|([1-4])|([0-9a-f]+)$")
        if not validID(id) or not title or #title>384 or #parsed>=128 then return false end
        title=hex(title);players,capacity=_G.tonumber(players),_G.tonumber(capacity)
        if not title or players>capacity then return false end
        local labels={o="Offen / Open",f="Voll / Full",s="Gestartet / Started",i="Andere Spieldaten / Incompatible"}
        parsed[#parsed+1]={id=id,enabled=state=="o" and players<capacity,
            text=title:gsub("[{}]","").."   "..players.."/"..capacity.."   "..labels[state]}
    end
    if list then list.removeAll();for _,row in _G.ipairs(parsed) do list.addItem(row) end end
    rows=parsed
    return true
end
local function close()
    if confirmation then confirmation.destroy();confirmation=nil end
    if frame then G.frame.destroy(frame);frame=nil end
    nativeVisible(true)
    list,notice,joinButton=nil,nil,nil;rows={}
end
local function join()
    local row=list and list.getSelectedItemData()
    if not row or not row.enabled or not validID(nonce) or G.Network.SessionIsInSession()
        or not lastChange or G.Game.GetRunTime()-lastChange>35000 then return end
    if confirmation then return end
    local selected,token=row.id,nonce
    confirmation=Confirm.create(Confirm.types.confirmYesNo,Strings.confirm,true,false,nil,nil,function(action)
        local accepted=action.userData
        confirmation.destroy();confirmation=nil
        if not accepted then return end
        if G.Network.SessionIsInSession() or token~=nonce or not lastChange or G.Game.GetRunTime()-lastChange>35000 then return end
        if bridge("weplay-directory-join-v1:"..token..":"..selected)==true then G.system.exitApi()
        else G.ui.messageBox("WePlay",Strings.unavailable) end
    end,"WePlay")
end
local function open()
    if frame or G.Network.SessionIsInSession() then return end
    nativeVisible(false)
    local width,height=G.system.getScreenSize()
    frame=G.frame.create("WePlay global games",State.browserFrame,45,205,width-90,height-340,G.defaultPriorities.todoHint+12)
    local bg=G.image.create(frame,{x=0,y=0},{name="whitepixel",u=0,v=0,w=1,h=1},G.UI.Anchor.Top,G.UI.Anchor.Left,true)
    G.image.setColor(bg,25,22,18,255);G.ui.setSize(bg,width-90,height-340)
    notice=G.label.create(frame,{x=20,y=10},{Strings.title,true},G.font.small,G.UI.Anchor.Top,G.UI.Anchor.Left,width-140)
    list=List.create(frame,{x=20,y=55},width-130,height-470,List.templates.genericListWithBackground,List.itemTemplates.singleTextItemTemplate,function()end)
    local refresh=G.button.create(frame,{x=20,y=height-405},{Strings.refresh,true},G.button.class.optionsButton,function()read()end,nil,G.UI.Anchor.Top,G.UI.Anchor.Left)
    G.ui.setSize(refresh,250,45)
    joinButton=G.button.create(frame,{x=285,y=height-405},{Strings.join,true},G.button.class.optionsButton,join,nil,G.UI.Anchor.Top,G.UI.Anchor.Left)
    G.ui.setSize(joinButton,220,45)
    local back=G.button.create(frame,{x=520,y=height-405},"Back",G.button.class.optionsButton,close,nil,G.UI.Anchor.Top,G.UI.Anchor.Left)
    G.ui.setSize(back,180,45)
    if not read() then G.ui.messageBox("WePlay",Strings.unavailable) end
end
function Panel.create(callbacks)
    create(callbacks)
    local width,height=G.system.getScreenSize()
    local supported=bridge("weplay-directory-read-v1")
    if supported~=nil then
        local button=G.button.create(State.browserFrame,{x=335,y=height-230},{Strings.title,true},G.button.class.optionsButton,open,nil,G.UI.Anchor.Top,G.UI.Anchor.Left)
        G.ui.setSize(button,230,45)
    end
end
function Panel.update()
    update()
    if not frame then return end
    if G.Network.SessionIsInSession() then close();return end
    local now=G.Game.GetRunTime()
    if not lastRead or now-lastRead>=1000 then
        lastRead=now
        -- Preserve the selected row while refreshing data and translated status.
        local selected=list.getSelectedItemData();local id=selected and selected.id
        local okay=read()
        if not okay then list.removeAll();rows={} end
        if id then for index,row in _G.ipairs(rows) do if row.id==id then list.setSelectedIndex(index-1) end end end
    end
    local selected=list.getSelectedItemData()
    if selected and selected.enabled then G.ui.enable(joinButton) else G.ui.disable(joinButton) end
end
function Panel.destroy() close();destroy() end
