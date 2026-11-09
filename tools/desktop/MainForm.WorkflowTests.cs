using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private void TestFreshReviewWorkflows(JsonElement original)
    {
        LoadSnapshot(original,acceptGraphics:true);ShowMain();tabs.SelectedTab=directoryPage;
        directoryAvailable=directoryConnected=true;
        using var listed=JsonDocument.Parse("""{"connected":true,"games":[{"id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","title":"Click fixture","compatible":true,"state":"open","free_slots":3,"players":1,"capacity":4,"version":"fixture"}]}""");
        ShowDirectory(listed.RootElement,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");Application.DoEvents();
        int joins=0,cancels=0;
        testJoinDirectory=id=>{
            if(id!="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"||!busy||!directoryLoadFailed)throw new Exception("Join must carry its pre-Guard selection into the exclusive operation");
            joins++;return Task.CompletedTask;
        };
        try
        {
            directoryJoin.PerformClick();Application.DoEvents();
            if(joins!=1||busy)throw new Exception("Actual public Join click did not reach its operation");
            directoryJoin.PerformClick();Application.DoEvents();
            if(joins!=1)throw new Exception("Stale public Join accepted a second click");
            internetOperation=publicRoomOperation=true;internetStage="ready";gameRunning=false;
            testCancellation=()=>cancels++;SetBusy(true);
            if(!directoryEnd.Enabled)throw new Exception("Public End is disabled before the game");
            directoryEnd.PerformClick();Application.DoEvents();
            if(cancels!=1||!cancelRequested||directoryEnd.Enabled)throw new Exception("Actual public End click did not cancel the active operation once");
            directoryEnd.PerformClick();if(cancels!=1)throw new Exception("Public End repeats cancellation");
            cancelRequested=false;gameRunning=true;UpdateDirectoryControls();directoryEnd.PerformClick();
            if(cancels!=1||directoryEnd.Enabled)throw new Exception("Public End cancels a live game");
        }
        finally{testJoinDirectory=null;testCancellation=null;gameRunning=false;internetOperation=publicRoomOperation=false;SetBusy(false);}

        // Exercise Save -> committed response without hardware -> edit -> Discard -> Save.
        // The backend normalizes this fixture to Native and clears the selected GPU.
        LoadSnapshot(original,acceptGraphics:true);tabs.SelectedTab=graphicsPage;
        qualities["TextureQuality"].SelectedIndex=2;int saves=0;
        testUiBackend=(action,data)=>{
            if(action!="save")throw new Exception("Unexpected workflow action: "+action);
            var request=JsonSerializer.SerializeToElement(data);
            if(saves>0&&(request.GetProperty("quality").GetProperty("TextureQuality").GetInt32()!=2
                ||request.GetProperty("renderer_policy").GetString()!="native"
                ||request.GetProperty("choice").GetProperty("choice").ValueKind!=JsonValueKind.Null))
                throw new Exception("Discard caused a subsequent Save to overwrite the committed generation");
            saves++;
            var selected=JsonNode.Parse(request.GetProperty("choice").GetRawText())!.AsObject();selected["choice"]=null;
            foreach(string flag in new[]{"midsummer_tone","enhanced_aa","contact_shadows","maximum_fx"})selected[flag]=false;
            return Task.FromResult(JsonSerializer.SerializeToElement(new {settings_committed=true,refresh_failed=true,
                committed_settings=new {renderer_policy="native",renderer="native",choice=selected,quality=request.GetProperty("quality"),player_name="Tester"}}));
        };
        try
        {
            Application.DoEvents();graphicsSave.PerformClick();Application.DoEvents();
            if(saves!=1||GraphicsDirty||renderer.SelectedIndex!=2||status.Text!=Lang.T("settings_saved_refresh_failed"))throw new Exception("Committed generation was not accepted after failed hardware refresh");
            qualities["TextureQuality"].SelectedIndex=0;renderer.SelectedIndex=1;
            graphicsDiscard.PerformClick();Application.DoEvents();
            if(GraphicsDirty||qualities["TextureQuality"].SelectedIndex!=2||renderer.SelectedIndex!=2)throw new Exception("Discard restored the obsolete pre-commit snapshot");
            graphicsSave.PerformClick();Application.DoEvents();if(saves!=2)throw new Exception("Save after Discard did not complete");
        }
        finally{testUiBackend=null;LoadSnapshot(original,acceptGraphics:true);}

        var unavailable=JsonNode.Parse(original.GetRawText())!.AsObject();unavailable["probe"]!["devices"]=new JsonArray();
        LoadSnapshot(JsonSerializer.SerializeToElement(unavailable),acceptGraphics:true);tabs.SelectedTab=graphicsPage;
        string before=CaptureGraphicsDraft();graphicsRecommended.PerformClick();graphicsMaximum.PerformClick();
        if(graphicsRecommended.Enabled||graphicsMaximum.Enabled||!graphicsPresetNotice.Visible||CaptureGraphicsDraft()!=before)
            throw new Exception("Unsupported Modern preset remains actionable or lacks an explanation");
        LoadSnapshot(original,acceptGraphics:true);midsummerToneAvailable=true;UpdateGraphicsPresets();
        graphicsRecommended.PerformClick();Application.DoEvents();
        if(renderer.SelectedIndex!=1||!enhancedAa.Checked)throw new Exception("Supported Recommended click failed");
        graphicsMaximum.PerformClick();Application.DoEvents();if(!maximumFx.Checked)throw new Exception("Supported Maximum click failed");
        LoadSnapshot(original,acceptGraphics:true);
        TestSettingsRecoveryWorkflow(original);
        tabs.SelectedTab=homePage;lastError="";details.Clear();status.Text=Lang.T("ready");
    }

    private void TestSettingsRecoveryWorkflow(JsonElement original)
    {
        int diagnoses=0,resolutions=0;bool pending=true,detached=false,stale=false;string token=new('a',64);
        JsonElement Status()=>JsonSerializer.SerializeToElement(new {pending,recoverable=true,can_detach=true,conflicts=new[]{"quality"},token});
        testRecoveryConfirmation=_=>false;
        testUiBackend=(action,data)=>{
            if(action=="recovery-status")return Task.FromResult(Status());
            if(action=="diagnostics"){diagnoses++;return Task.FromResult(Status());}
            if(action=="recovery-resolve")
            {
                var request=JsonSerializer.SerializeToElement(data);
                if(request.GetProperty("token").GetString()!=token)throw new Exception("Recovery used an obsolete confirmation token");
                if(stale){stale=false;token=new('b',64);throw new BackendError("recovery_changed","");}
                resolutions++;pending=false;detached=request.GetProperty("action").GetString()=="detach";
                return Task.FromResult(JsonSerializer.SerializeToElement(new {resolved=true,action=request.GetProperty("action").GetString(),backup="settings-recovery-fixture.json"}));
            }
            if(action=="snapshot")
            {
                if(pending)throw new BackendError("preferences_invalid","");
                var result=JsonNode.Parse(original.GetRawText())!.AsObject();result["installed"]=!detached;
                return Task.FromResult(JsonSerializer.SerializeToElement(result));
            }
            if(action=="detect")return Task.FromResult(JsonSerializer.SerializeToElement(new {sources=Array.Empty<object>()}));
            throw new Exception("Unexpected recovery action: "+action);
        };
        try
        {
            AwaitUi(Guard(async()=>{await RunUiBackend("snapshot",new {});}));Application.DoEvents();
            if(!recoveryVisible||!settingsRecovery.Visible||tabs.Visible||setup.Visible)throw new Exception("Conflict has no dedicated recovery journey");
            recoveryDiagnostics.PerformClick();Application.DoEvents();if(diagnoses!=1)throw new Exception("Conflict blocks read-only diagnostics");
            recoveryRestore.PerformClick();Application.DoEvents();if(resolutions!=0||!pending)throw new Exception("Declined recovery performed a mutation");
            testRecoveryConfirmation=_=>true;stale=true;
            recoveryRestore.PerformClick();Application.DoEvents();
            if(resolutions!=0||!recoveryVisible||recoveryStatus.GetProperty("token").GetString()!=token)throw new Exception("Stale recovery confirmation was not refreshed safely");
            recoveryRestore.PerformClick();Application.DoEvents();
            if(resolutions!=1||recoveryVisible||!tabs.Visible||!status.Text.Contains("settings-recovery-fixture.json"))throw new Exception("Restored settings did not return to the launcher with a backup receipt");
            pending=true;ShowSettingsRecovery(Status());UpdateRecoveryControls();
            recoveryDetach.PerformClick();Application.DoEvents();
            if(resolutions!=2||recoveryVisible||!setup.Visible||tabs.Visible)throw new Exception("Explicit detach did not lead to setup");
        }
        finally
        {
            testUiBackend=null;testRecoveryConfirmation=null;recoveryVisible=false;settingsRecovery.Visible=false;
            LoadSnapshot(original,acceptGraphics:true);ShowMain();
        }
    }
}
