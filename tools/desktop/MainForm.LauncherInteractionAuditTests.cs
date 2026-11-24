using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private static void TestLauncherInteractions(JsonElement fixture)
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
            finally{form.gameRunning=false;form.busy=false;form.savedGraphicsDraft=null;form.Close();}
        }

        Check("graphics-prevalidates-GPU-display-pair",form=>{
            var twoGpus=JsonNode.Parse(fixture.GetRawText())!.AsObject();
            twoGpus["probe"]!["devices"]=new JsonArray("Fixture GPU","Other GPU");
            form.LoadSnapshot(JsonSerializer.SerializeToElement(twoGpus),acceptGraphics:true);
            form.tabs.SelectedTab=form.graphicsPage;
            form.gpu.SelectedIndex=2;
            if(form.graphicsSave.Enabled||!form.graphicsDisplayUnavailable.Visible)
                throw new Exception("The launcher offered to save a GPU that cannot drive the selected display.");
            form.gpu.SelectedIndex=0;
            if(!form.graphicsSave.Enabled)
                throw new Exception("Automatic GPU selection should restore Save.");
        });

        Check("invalid-LAN-name-does-not-save-port",form=>{
            int portSaves=0;
            form.testUiBackend=(action,data)=>{
                if(action!="lan-port")throw new Exception("Unexpected launcher action "+action);
                portSaves++;
                return Task.FromResult(JsonSerializer.SerializeToElement(new {lan_discovery_port=19112}));
            };
            form.player.Clear();form.lanDiscoveryPort.Value=19112;
            try{AwaitUi(form.Play(true));throw new Exception("The empty LAN name was accepted.");}
            catch(BackendError error)when(error.Code=="nameInvalid"){}
            if(portSaves!=0||form.savedLanDiscoveryPort==19112)
                throw new Exception("The invalid play request persisted a pending port.");
        });

        Check("declining-consent-keeps-unrelated-player-name-drafts",form=>{
            form.internetAvailable=true;
            form.player.Text="LAN draft";form.internetPlayer.Text="Private draft";form.directoryPlayer.Text="Directory draft";
            form.internetJoin.Text="weplay1.synthetic";
            form.testInternetConsent=publicLobby=>false;
            AwaitUi(form.PlayInternet(false));
            AwaitUi(form.JoinDirectoryGame("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
            if(form.player.Text!="LAN draft"||form.internetPlayer.Text!="Private draft")
                throw new Exception("Declining a room connection transferred unrelated names.");
        });

        Check("declining-public-hosting-keeps-private-form",form=>{
            form.directoryAvailable=form.directoryConnected=form.publicHostingAvailable=form.internetAvailable=true;
            form.internetPlayer.Text="Private draft";form.directoryPlayer.Text="Directory draft";
            form.internetMode.SelectedIndex=1;form.internetGuestChoice.Checked=true;
            form.testInternetConsent=publicLobby=>false;
            form.tabs.SelectedTab=form.directoryPage;form.UpdateDirectoryControls();Application.DoEvents();
            if(!form.directoryHost.Enabled)throw new Exception("The public-host fixture could not exercise the button.");
            form.directoryHost.PerformClick();Application.DoEvents();
            if(form.internetPlayer.Text!="Private draft"||form.internetMode.SelectedIndex!=1||!form.internetGuestChoice.Checked)
                throw new Exception("A declined public session reset the private form.");
        });

        Check("guest-policy-keeps-pending-host-mode",form=>{
            form.internetOperation=true;form.internetGuestChoice.Checked=true;form.internetMode.SelectedIndex=0;
            form.Receive(JsonSerializer.SerializeToElement(new {kind="internet_policy",mode="direct"}));
            if(form.internetConnection.Text!=Lang.T("internetConnectionDirect"))
                throw new Exception("A direct guest connection was not displayed as direct.");
            form.internetOperation=false;form.internetHostChoice.Checked=true;form.UpdateInternetControls();
            if(form.internetMode.SelectedIndex!=0)
                throw new Exception("Joining a direct room changed the next host's preferred type.");
        });

        Check("ordinary-invitation-field-routes-typed-mixed-code",form=>{
            var supportsMixed=JsonNode.Parse(fixture.GetRawText())!.AsObject();
            supportsMixed["crossplay_available"]=true;supportsMixed["internet_available"]=true;
            form.LoadSnapshot(JsonSerializer.SerializeToElement(supportsMixed),acceptGraphics:true);
            form.internetGuestChoice.Checked=true;
            form.internetJoin.Text="weplayx1.synthetic";
            if(!form.MixedInternet)
                throw new Exception("A typed mixed invitation left the History-only route selected.");
            form.internetJoin.Text="weplay1.synthetic";
            if(form.MixedInternet)
                throw new Exception("Replacing a mixed invitation left the mixed route selected.");
        });

        Check("moving-window-keeps-automatic-layout",form=>{
            // WinForms raises ResizeBegin and ResizeEnd for both moving and resizing.
            static void Raise(MainForm target,string method)=>typeof(Form)
                .GetMethod(method,BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(target,[EventArgs.Empty]);
            form.automaticMenuHeight=true;
            Raise(form,"OnResizeBegin");form.Left+=25;Raise(form,"OnResizeEnd");
            if(!form.automaticMenuHeight)
                throw new Exception("Moving the window switched off automatic menu height.");
            Raise(form,"OnResizeBegin");form.Width+=25;Raise(form,"OnResizeEnd");
            if(form.automaticMenuHeight)
                throw new Exception("Manually resizing the window should retain the user's size.");
        });

        Check("diagnostic-probe-keeps-unfinished-input",form=>{
            form.player.Text="LAN draft";form.lanDiscoveryPort.Value=19112;
            form.testUiBackend=(action,data)=>Task.FromResult(action switch
            {
                "probe"=>JsonSerializer.SerializeToElement(new {}),
                "snapshot"=>fixture,
                _=>throw new Exception("Unexpected graphics diagnostic action "+action)
            });
            AwaitUi(form.Probe());
            if(form.player.Text!="LAN draft"||form.lanDiscoveryPort.Value!=19112)
                throw new Exception("Checking graphics hardware reset text or a pending port.");
        });

        Check("finished-public-guest-is-not-advertised-as-host",form=>{
            form.publicRoomOperation=form.internetOperation=form.gameRunning=true;
            form.internetCreating=false;
            form.Receive(JsonSerializer.SerializeToElement(new {kind="game_finished"}));
            if(form.directoryProgress.Text!=Lang.T("internetGuestEnding"))
                throw new Exception("The guest was told they were hosting after their game exited.");
        });

        if(failures.Count>0)throw new Exception(string.Join(Environment.NewLine,failures));
    }
}
