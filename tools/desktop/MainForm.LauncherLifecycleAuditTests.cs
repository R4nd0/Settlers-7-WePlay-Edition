using System.Reflection;
using System.Text.Json;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private static void TestLauncherLifecycle(JsonElement fixture)
    {
        var failures=new List<string>();
        void Check(string name,Action<MainForm> exercise)
        {
            using var form=new MainForm(test:true) {
                StartPosition=FormStartPosition.Manual,Location=new Point(-30000,-30000),
                ShowInTaskbar=false,Opacity=0
            };
            try
            {
                form.LoadSnapshot(fixture,acceptGraphics:true);
                form.ShowMain();form.Show();Application.DoEvents();
                exercise(form);
            }
            catch(Exception error){failures.Add(name+" — "+error.Message);}
            finally
            {
                form.CloseGameInvitation();
                form.gameRunning=false;form.busy=false;form.savedGraphicsDraft=null;form.Close();
            }
        }

        const string gameId="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        Check("failed-action-does-not-open-the-next-room-consent",form=>{
            form.internetAvailable=true;form.pendingDirectoryGame=gameId;
            int prompts=0;
            form.testInternetConsent=_=>{prompts++;return false;};
            form.testUiBackend=(action,data)=>Task.FromResult(JsonSerializer.SerializeToElement(new {pending=false}));
            AwaitUi(form.Guard(()=>Task.FromException(new BackendError("operation_failed","Synthetic failure before room switch"))));
            if(prompts!=0||form.pendingDirectoryGame!=null)
                throw new Exception("A failed operation started the queued room switch.");
        });

        Check("stale-directory-switch-progress-does-not-replace-the-game-state",form=>{
            form.Receive(JsonSerializer.SerializeToElement(new {kind="game_started"}));
            form.Receive(JsonSerializer.SerializeToElement(new {kind="directory_switch",game_id=gameId}));
            if(form.status.Text!=Lang.T("running")||form.pendingDirectoryGame!=null)
                throw new Exception("A delayed progress event replaced the running game's status with a consent notice.");
        });

        Check("public-host-end-action-follows-its-role",form=>{
            form.tabs.SelectedTab=form.directoryPage;
            form.internetOperation=form.publicRoomOperation=form.internetCreating=true;
            form.internetStage="hosting";form.UpdateDirectoryControls();
            if(!form.directoryEnd.Enabled||form.directoryEnd.Text!=Lang.T("internetEnd"))
                throw new Exception("The public host was offered a guest's Leave action label.");
            form.internetCreating=false;form.UpdateDirectoryControls();
            if(form.directoryEnd.Text!=Lang.T("internetLeave"))
                throw new Exception("The public guest must still see Leave.");
        });

        Check("incomplete-invitation-notification-cannot-interrupt-an-active-game",form=>{
            form.gameRunning=form.internetOperation=form.internetCreating=true;
            form.invitationRun=gameId;
            form.Receive(JsonSerializer.SerializeToElement(new {kind="invitation_requested",run=gameId}));
            form.publicRoomOperation=true;
            form.Receive(JsonSerializer.SerializeToElement(new {kind="invitation_requested",run=gameId,mode="public"}));
            if(form.invitationDialog!=null||!form.gameRunning)
                throw new Exception("An incomplete local invitation notification changed the running game.");
        });

        Check("invalid-next-game-id-is-never-queued",form=>{
            form.QueueDirectorySwitch(JsonSerializer.SerializeToElement(new {directory_game_id="missing"}));
            if(form.pendingDirectoryGame!=null)
                throw new Exception("The launcher queued a room switch without a usable game ID.");
            form.QueueDirectorySwitch(JsonSerializer.SerializeToElement(new {directory_game_id=gameId}));
            if(form.pendingDirectoryGame!=gameId)
                throw new Exception("A valid next-game selection was dropped.");
        });

        Check("escape-dismisses-the-modeless-game-invitation",form=>{
            form.gameRunning=form.internetOperation=form.internetCreating=true;
            form.invitationRun=gameId;form.internetInvite.Text="weplay1.synthetic";
            form.Receive(JsonSerializer.SerializeToElement(new {kind="invitation_requested",run=gameId,mode="private"}));
            var dialog=form.invitationDialog??throw new Exception("Invitation fixture did not open.");
            typeof(Form).GetMethod("ProcessDialogKey",BindingFlags.Instance|BindingFlags.NonPublic)!
                .Invoke(dialog,[Keys.Escape]);
            if(form.invitationDialog!=null)
                throw new Exception("Escape did not dismiss the in-game invitation.");
        });

        Check("warning-does-not-hide-a-pending-cancellation",form=>{
            form.SetBusy(true);form.RequestCancellation(()=>{});
            form.Receive(JsonSerializer.SerializeToElement(new {kind="warning",code="memory_physical_warning"}));
            if(!form.cancelRequested||form.status.Text!=Lang.T("cancelRequested"))
                throw new Exception("A warning hid the visible status of the pending cancellation.");
        });

        if(failures.Count>0)throw new Exception(string.Join(Environment.NewLine,failures));
    }
}
