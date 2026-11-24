using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private string? savedGraphicsDraft;
    private bool closingDraft,closeDraftApproved,changingLanguage;
    private readonly Label graphicsDraftNotice=new() {AutoSize=true,MaximumSize=new Size(840,0),Margin=new Padding(0,4,0,2)};
    private Button graphicsDiscard=null!,graphicsSave=null!,graphicsRestore=null!;
    private string? graphicsBackup;
    private bool GraphicsDirty=>savedGraphicsDraft!=null&&savedGraphicsDraft!=CaptureGraphicsDraft();
    private string? GraphicsDisplayIssue
    {
        get
        {
            if(keepDisplay.Checked)return null;
            int index=monitor.SelectedIndex;
            if(index<0||index>=screens.Count||resolution.SelectedItem is not Item)return "graphicsDisplayUnavailable";
            if(RendererMode=="dxvk"&&gpu.SelectedItem is Item {Key.Length:>0} selected
                &&!string.Equals(screens[index].GetProperty("adapter").GetString(),selected.Key,StringComparison.OrdinalIgnoreCase))
                return "graphicsDisplayGpuMismatch";
            return null;
        }
    }

    private string CaptureGraphicsDraft()=>JsonSerializer.Serialize(new {
        renderer=RendererMode,
        antialiasing=AntialiasingMode,
        detail=detailLevel.Value,
        gpu=(gpu.SelectedItem as Item)?.Key,
        keepDisplay=keepDisplay.Checked,monitor=keepDisplay.Checked?null:(monitor.SelectedItem as Item)?.Key,
        window=keepDisplay.Checked?-1:window.SelectedIndex,resolution=keepDisplay.Checked?null:(resolution.SelectedItem as Item)?.Key,
        quality=qualities.ToDictionary(x=>x.Key,x=>x.Value.SelectedIndex),
        effects=effects.ToDictionary(x=>x.Key,x=>x.Value.Checked),
        flags=new[]{simple,anisotropicFiltering,linkShadowRange,detailedShadows,extendedShadowRange,
            ultraShadows,midsummerTone,contactShadows,maximumFx}.Select(x=>x.Checked).ToArray()
    });
    private void AcceptGraphicsDraft(){savedGraphicsDraft=CaptureGraphicsDraft();UpdateGraphicsDraft();}
    private void ApplySettingsResult(JsonElement value)
    {
        bool refreshFailed=value.TryGetProperty("refresh_failed",out var failed)&&failed.GetBoolean();
        if(refreshFailed)
        {
            if(!value.GetProperty("settings_committed").GetBoolean())throw new BackendError("backend_protocol_invalid","");
            // Hardware can be unavailable after commit. Retain its last snapshot,
            // but replace ALL persisted settings with the authoritative generation.
            var merged=JsonNode.Parse(snapshot.GetRawText())!.AsObject();
            foreach(var field in value.GetProperty("committed_settings").EnumerateObject())
                merged[field.Name]=JsonNode.Parse(field.Value.GetRawText());
            LoadGraphicsSettings(JsonSerializer.SerializeToElement(merged));
        }
        else LoadGraphicsSettings(value);
        status.Text=Lang.T(refreshFailed?"settings_saved_refresh_failed":"saved");
    }
    private void UpdateGraphicsDraft()
    {
        if(loading)return;
        bool dirty=GraphicsDirty;
        graphicsDraftNotice.Text=Lang.T(dirty?"graphicsUnsaved":"graphicsSavedDraft");
        graphicsDraftNotice.ForeColor=dirty?Color.FromArgb(137,87,15):SystemColors.GrayText;
        string? displayIssue=GraphicsDisplayIssue;
        if(graphicsSave!=null)graphicsSave.Enabled=!busy&&displayIssue==null;
        graphicsDisplayUnavailable.Visible=displayIssue!=null;
        if(displayIssue!=null)graphicsDisplayUnavailable.Text=Lang.T(displayIssue);
        if(graphicsDiscard!=null)graphicsDiscard.Enabled=dirty&&!busy;
        if(graphicsRestore!=null)
        {
            graphicsRestore.Text=Lang.T(graphicsBackup switch
            {
                "confirmed"=>"graphicsRestoreConfirmed",
                "previous"=>"graphicsRestorePrevious",
                _=>"graphicsUseSafe"
            });
            graphicsRestore.Enabled=!busy;
        }
    }
    private void TrackGraphicsDraft()
    {
        foreach(var combo in new[]{renderer,gpu,monitor,window,resolution,antialiasing}.Concat(qualities.Values))
            combo.SelectedIndexChanged+=(_,_)=>UpdateGraphicsDraft();
        detailLevel.ValueChanged+=(_,_)=>UpdateGraphicsDraft();
        foreach(var box in new[]{keepDisplay,simple,anisotropicFiltering,linkShadowRange,detailedShadows,extendedShadowRange,
            ultraShadows,midsummerTone,contactShadows,maximumFx}.Concat(effects.Values))
            box.CheckedChanged+=(_,_)=>UpdateGraphicsDraft();
    }
    private void DiscardGraphicsDraft()
    {
        if(snapshot.ValueKind==JsonValueKind.Object)LoadGraphicsSettings(snapshot);
    }
    private void LoadGraphicsSettings(JsonElement value)
    {
        // Graphics operations leave independent inputs as the player entered them.
        string name=player.Text,internetName=internetPlayer.Text,directoryName=directoryPlayer.Text,storage=destination.Text;
        bool publish=discordPublish.Checked;
        decimal port=lanDiscoveryPort.Value;
        int savedPort=savedLanDiscoveryPort;
        LoadSnapshot(value,acceptGraphics:true);
        player.Text=name;internetPlayer.Text=internetName;directoryPlayer.Text=directoryName;destination.Text=storage;discordPublish.Checked=publish;
        savedLanDiscoveryPort=savedPort;lanDiscoveryPort.Value=port;
    }
    private async Task<bool> ResolveGraphicsDraft(DialogResult? decision=null,Func<Task>? save=null)
    {
        if(!GraphicsDirty)return true;
        var answer=decision??MessageBox.Show(this,Lang.T("graphicsDraftQuestion"),Lang.T("graphics"),
            MessageBoxButtons.YesNoCancel,MessageBoxIcon.Question);
        if(answer==DialogResult.Cancel)return false;
        if(answer==DialogResult.Yes){if(save!=null)await save();else await SaveGraphics();}
        else if(answer==DialogResult.No)DiscardGraphicsDraft();
        else return false;
        return true;
    }
    private void ChangeLanguage(Action<string,bool>? persist=null)
    {
        if(changingLanguage||language.SelectedItem is not Item item)return;
        string previous=Lang.Current;
        try{(persist??Lang.Set)(item.Key,!testing);ApplyLanguage();FitMenuHeight();UpdateGraphicsDraft();}
        catch(Exception error)when(error is IOException or UnauthorizedAccessException)
        {
            Lang.Set(previous,false);changingLanguage=true;
            try{language.SelectedIndex=previous=="de"?0:1;ApplyLanguage();}
            finally{changingLanguage=false;}
            ShowError(new BackendError("language_save_failed",error.Message));
        }
    }
    private void RequestCancellation(Action? request=null)
    {
        if(cancelRequested)return;
        if(testing&&request==null)request=testCancellation;
        if(request==null&&Backend.Operation==null)return;
        try
        {
            (request??Backend.Cancel)();cancelRequested=true;cancelButton.Enabled=false;
            status.Text=Lang.T("cancelRequested");UpdateInternetControls();UpdateDirectoryControls();
        }
        catch(Exception error)when(error is BackendError or IOException or UnauthorizedAccessException)
        {ShowError(error is BackendError?error:new BackendError("cancel_request_failed",error.Message));}
    }
    private async Task EnsureMissingShortcuts()
    {
        try{await Shortcuts.EnsureMissingAsync();}
        catch(Exception error)when(error is IOException or UnauthorizedAccessException or System.Runtime.InteropServices.COMException)
        {if(!IsDisposed&&!busy)status.Text=Lang.T("shortcuts_deferred");}
    }
}
