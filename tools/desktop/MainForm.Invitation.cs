using System.Runtime.InteropServices;
using System.Text.Json;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private string? invitationRun;
    private Form? invitationDialog;
    private Action<string> invitationCopy=Clipboard.SetText;

    private void CloseGameInvitation()
    {
        invitationRun=null;
        invitationDialog?.Close();invitationDialog=null;
    }

    private void OpenGameInvitation(JsonElement value)
    {
        // Delayed events from a previous game may never expose a later room.
        if(!gameRunning||!internetOperation||!internetCreating||invitationRun is null||
            !value.TryGetProperty("run",out var run)||run.ValueKind!=JsonValueKind.String||run.GetString()!=invitationRun||
            !value.TryGetProperty("mode",out var requestMode)||requestMode.ValueKind!=JsonValueKind.String)return;
        string? mode=requestMode.GetString();
        string requestedRun=invitationRun;
        if(mode!=(publicRoomOperation?"public":"private"))return;
        if(invitationDialog is not null){if(!testing)invitationDialog.Activate();return;}
        string code;
        if(mode=="private")
        {
            code=internetInvite.Text;
            if(!code.StartsWith("weplay1.",StringComparison.Ordinal)||code.Length>2048)return;
        }
        else
        {
            if(!value.TryGetProperty("code",out var requestCode)||requestCode.ValueKind!=JsonValueKind.String)return;
            code=requestCode.GetString()??"";
            if(!code.StartsWith("weplay-directory1.",StringComparison.Ordinal)||code.Length>2048)return;
        }
        if(!testing)RestoreWindow();
        var dialog=new Form {Text=Lang.T("gameInvitationTitle"),ClientSize=new Size(560,210),
            StartPosition=FormStartPosition.CenterParent,FormBorderStyle=FormBorderStyle.FixedDialog,
            MaximizeBox=false,MinimizeBox=false,ShowInTaskbar=false};
        var info=new Label {Text=Lang.T(mode=="public"?"gameInvitationPublic":"gameInvitationPrivate"),
            Left=20,Top=20,Width=520,Height=110};
        var copy=new Button {Name="copyInvitation",Text=Lang.T("gameInvitationCopy"),Left=20,Top=150,Width=240,Height=38};
        var close=new Button {Text=Lang.T("gameInvitationClose"),Left=280,Top=150,Width=140,Height=38,DialogResult=DialogResult.Cancel};
        close.Click+=(_,_)=>dialog.Close();
        copy.Click+=(_,_)=>{
            if(!gameRunning||requestedRun!=invitationRun)return;
            try{invitationCopy(code);dialog.Close();}
            catch(ExternalException){info.Text=Lang.T("gameInvitationClipboardFailed");}
        };
        dialog.Controls.AddRange([info,copy,close]);
        dialog.CancelButton=close;
        invitationDialog=dialog;
        dialog.FormClosed+=(_,_)=>{code="";if(ReferenceEquals(invitationDialog,dialog))invitationDialog=null;dialog.Dispose();};
        if(testing){dialog.StartPosition=FormStartPosition.Manual;dialog.Location=new Point(-30000,-30000);}
        dialog.Show(this);if(!testing)dialog.Activate();
    }

    private void TestGameInvitation(string folder,string language)
    {
        string? copied=null;invitationCopy=value=>copied=value;
        internetOperation=true;internetCreating=true;gameRunning=true;publicRoomOperation=false;
        internetInvite.Text="weplay1.synthetic-invitation";invitationRun=new string('a',32);
        using var current=JsonDocument.Parse("""{"kind":"invitation_requested","run":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","mode":"private"}""");
        using var stale=JsonDocument.Parse("""{"kind":"invitation_requested","run":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","mode":"private"}""");
        Receive(stale.RootElement);if(invitationDialog is not null)throw new Exception("Stale invitation request accepted");
        internetCreating=false;Receive(current.RootElement);if(invitationDialog is not null)throw new Exception("Guest invitation request accepted");
        internetCreating=true;Receive(current.RootElement);
        var opened=invitationDialog??throw new Exception("Host invitation dialog missing");
        Receive(current.RootElement);if(!ReferenceEquals(opened,invitationDialog))throw new Exception("Repeated click created another dialog");
        invitationCopy=_=>throw new ExternalException("Synthetic clipboard contention");
        ((Button)opened.Controls["copyInvitation"]!).PerformClick();
        if(invitationDialog is null||copied is not null)throw new Exception("Clipboard failure reported success");
        invitationCopy=value=>copied=value;
        Application.DoEvents();using(var bitmap=new Bitmap(opened.Width,opened.Height))
        {opened.DrawToBitmap(bitmap,new Rectangle(0,0,opened.Width,opened.Height));bitmap.Save(Path.Combine(folder,language+"-game-invitation.png"));}
        ((Button)opened.Controls["copyInvitation"]!).PerformClick();
        if(copied!="weplay1.synthetic-invitation"||invitationDialog is not null)throw new Exception("Private invitation copy failed");
        publicRoomOperation=true;copied=null;
        Receive(current.RootElement);if(invitationDialog is not null)throw new Exception("Public room accepted private invitation action");
        using var publicRequest=JsonDocument.Parse("""{"kind":"invitation_requested","run":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","mode":"public","code":"weplay-directory1.synthetic-public-directory"}""");
        Receive(publicRequest.RootElement);
        Application.DoEvents();using(var bitmap=new Bitmap(invitationDialog!.Width,invitationDialog.Height))
        {invitationDialog.DrawToBitmap(bitmap,new Rectangle(0,0,bitmap.Width,bitmap.Height));bitmap.Save(Path.Combine(folder,language+"-public-game-invitation.png"));}
        ((Button)invitationDialog!.Controls["copyInvitation"]!).PerformClick();
        if(copied!="weplay-directory1.synthetic-public-directory")throw new Exception("Public invitation exposed private room code");
        Receive(publicRequest.RootElement);CloseGameInvitation();
        if(invitationDialog is not null||invitationRun is not null)throw new Exception("Game exit left an invitation dialog alive");
        Receive(publicRequest.RootElement);if(invitationDialog is not null)throw new Exception("Post-exit request accepted");
        invitationCopy=Clipboard.SetText;internetOperation=false;internetCreating=false;gameRunning=false;publicRoomOperation=false;internetInvite.Clear();
    }
}
