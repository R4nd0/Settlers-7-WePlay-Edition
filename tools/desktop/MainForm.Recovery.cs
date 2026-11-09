using System.Text.Json;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private readonly Panel settingsRecovery=new() {Dock=DockStyle.Fill,Visible=false};
    private readonly Label recoverySummary=new() {AutoSize=true,MaximumSize=new Size(840,0)};
    private Button recoveryRestore=null!,recoveryDetach=null!,recoveryRetry=null!,recoveryDiagnostics=null!;
    private JsonElement recoveryStatus;
    private bool recoveryVisible;

    // In-process fixture boundaries. They cannot be activated by environment or IPC.
    private Func<string,object,Task<JsonElement>>? testUiBackend;
    private Func<string,Task>? testJoinDirectory;
    private Action? testCancellation;
    private Func<string,bool>? testRecoveryConfirmation;
    private Task<JsonElement> RunUiBackend(string action,object data)=>testing&&testUiBackend!=null
        ?testUiBackend(action,data):Backend.Run(action,data,Receive);

    private void BuildSettingsRecovery()
    {
        body.Controls.Add(settingsRecovery);
        var scroll=new Panel {Dock=DockStyle.Fill,AutoScroll=true};settingsRecovery.Controls.Add(scroll);
        var stack=Stack(scroll);
        var title=Label("settingsRecoveryTitle");title.Font=new Font(Font.FontFamily,20,FontStyle.Bold);Add(stack,title);
        Add(stack,Label("settingsRecoveryInfo"));Add(stack,recoverySummary);
        recoveryRestore=Button("settingsRecoveryRestore",()=>ResolveSettingsRecovery("restore_previous"),true);
        recoveryDetach=Button("settingsRecoveryDetach",()=>ResolveSettingsRecovery("detach"));
        recoveryDiagnostics=Button("diagnostics",Diagnostics);
        recoveryRetry=Button("settingsRecoveryRetry",RefreshSettingsRecovery);
        Add(stack,Label("settingsRecoveryBackupInfo"));
        DockActions(settingsRecovery,Buttons(recoveryRestore,recoveryDetach,recoveryDiagnostics,recoveryRetry));
    }
    private void UpdateRecoveryControls()
    {
        if(recoveryRestore==null)return;
        bool pending=recoveryStatus.ValueKind==JsonValueKind.Object&&recoveryStatus.GetProperty("pending").GetBoolean();
        recoveryRestore.Enabled=!busy&&pending&&recoveryStatus.GetProperty("recoverable").GetBoolean();
        recoveryDetach.Enabled=!busy&&pending&&recoveryStatus.TryGetProperty("can_detach",out var detach)&&detach.GetBoolean();
        recoveryRetry.Enabled=recoveryDiagnostics.Enabled=!busy;
    }
    private void ShowSettingsRecovery(JsonElement value)
    {
        recoveryStatus=value.Clone();recoveryVisible=true;settingsRecovery.Visible=true;settingsRecovery.BringToFront();setup.Visible=tabs.Visible=false;
        var conflicts=value.GetProperty("conflicts").EnumerateArray().Select(item=>Lang.T("settingsRecoveryField_"+item.GetString()));
        recoverySummary.Text=Lang.T(value.GetProperty("recoverable").GetBoolean()?"settingsRecoveryCanRestore":
            value.TryGetProperty("can_detach",out var detachable)&&detachable.GetBoolean()?"settingsRecoveryCannotRestore":"settingsRecoveryManual")+
            (value.GetProperty("conflicts").GetArrayLength()>0?"\n\n"+Lang.T("settingsRecoveryConflicts")+" "+string.Join(", ",conflicts):"");
        status.Text=Lang.T("settingsRecoveryTitle");UpdateRecoveryControls();
    }
    private async Task<bool> TryShowSettingsRecovery(Exception error)
    {
        if(error is not BackendError failure||failure.Code is not ("preferences_invalid" or "installation_invalid" or "recovery_changed"))return false;
        try
        {
            var value=await RunUiBackend("recovery-status",new {});
            if(!value.GetProperty("pending").GetBoolean())return false;
            ShowSettingsRecovery(value);return true;
        }
        catch(Exception){return false;} // Keep the original actionable error if status is unavailable.
    }
    private async Task RefreshSettingsRecovery()
    {
        var value=await RunUiBackend("recovery-status",new {});
        if(value.GetProperty("pending").GetBoolean()){ShowSettingsRecovery(value);return;}
        await ResumeAfterRecovery();
    }
    private async Task ResolveSettingsRecovery(string action)
    {
        string question=Lang.T(action=="detach"?"settingsRecoveryDetachConfirm":"settingsRecoveryRestoreConfirm");
        bool confirmed=testing&&testRecoveryConfirmation!=null?testRecoveryConfirmation(action):
            MessageBox.Show(this,question,Lang.T("settingsRecoveryTitle"),MessageBoxButtons.YesNo,MessageBoxIcon.Warning,MessageBoxDefaultButton.Button2)==DialogResult.Yes;
        if(!confirmed)return;
        var result=await RunUiBackend("recovery-resolve",new {token=recoveryStatus.GetProperty("token").GetString(),action});
        await ResumeAfterRecovery();
        status.Text=string.Format(Lang.T("settingsRecoveryDone"),result.GetProperty("backup").GetString());
    }
    private async Task ResumeAfterRecovery()
    {
        var value=await RunUiBackend("snapshot",new {});
        savedGraphicsDraft=null;LoadSnapshot(value,acceptGraphics:true);
        recoveryVisible=false;settingsRecovery.Visible=false;recoverState=false;
        if(!value.GetProperty("installed").GetBoolean()||value.GetProperty("update_available").GetBoolean()
            ||(value.TryGetProperty("needs_repair",out var repair)&&repair.GetBoolean()))await ShowSetup();
        else ShowMain();
    }
}
