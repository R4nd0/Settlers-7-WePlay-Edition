using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private static void TestLauncherDirectoryState(JsonElement fixture)
    {
        var failures=new List<string>();
        void Check(string name,Action<MainForm> exercise)
        {
            string locale=Lang.Current;
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
            finally{Lang.Set(locale,false);form.gameRunning=false;form.busy=false;form.savedGraphicsDraft=null;form.Close();}
        }

        const string gameId="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        JsonElement Listing(string title="Directory test match")=>
            JsonSerializer.SerializeToElement(new {connected=true,games=new[]{
                new {id=gameId,title,state="open",compatible=true,free_slots=3,version="rc4.10",players=1,capacity=4}
            }});

        Check("public-play-card-reports-its-explicit-refresh-failure",form=>{
            form.directoryAvailable=true;
            var fetched=new TaskCompletionSource<JsonElement>();
            int requests=0;
            form.testDirectoryFetch=_=>{requests++;return fetched.Task;};
            form.playModes.Single(entry=>entry.Card.Mode==PlayMode.PublicInternet).Card.PerformClick();
            CheckStarted();
            fetched.SetException(new BackendError("directory_unavailable","Synthetic public page fetch"));
            Application.DoEvents();
            if(form.lastError!="Synthetic public page fetch")
                throw new Exception("An automatic refresh suppressed the explicit Open Public action's failure.");
            void CheckStarted()
            {
                if(requests!=1||!form.directoryRefreshing)
                    throw new Exception("Opening the directory did not start exactly one refresh.");
            }
        });

        Check("local-graphics-navigation-keeps-directory-fresh",form=>{
            form.directoryAvailable=true;form.ShowDirectory(Listing(),gameId);
            form.tabs.SelectedTab=form.homePage;Application.DoEvents();
            form.openGraphics.PerformClick();
            if(form.tabs.SelectedTab!=form.graphicsPage||form.directoryLoadFailed)
                throw new Exception("Local tab navigation marked an unchanged backend listing stale.");
        });

        Check("stale-Discord-option-does-not-block-anonymous-hosting",form=>{
            form.directoryAvailable=form.directoryConnected=form.publicHostingAvailable=form.internetAvailable=true;
            form.discordAvailable=true;form.discordConnected=true;form.discordPublish.Checked=true;
            form.discordConnected=false;form.UpdateInternetControls();
            int prompts=0;
            form.testInternetConsent=publicRoom=>{if(publicRoom)prompts++;return false;};
            form.tabs.SelectedTab=form.directoryPage;Application.DoEvents();
            form.directoryHost.PerformClick();
            if(prompts!=1||form.discordPublish.Checked)
                throw new Exception("A disconnected, disabled publishing choice blocked public hosting.");
        });

        Check("directory-accessible-name-follows-language",form=>{
            form.language.SelectedIndex=Lang.Current=="de"?1:0;
            if(form.directoryGames.AccessibleName!=Lang.T("directoryTitle"))
                throw new Exception("The public game table's spoken name stayed in the former language.");
        });

        Check("disconnected-directory-does-not-show-old-game-list",form=>{
            form.directoryAvailable=true;form.ShowDirectory(Listing(),gameId);
            var disconnected=JsonNode.Parse(fixture.GetRawText())!.AsObject();
            disconnected["directory_available"]=true;disconnected["directory_connected"]=false;
            form.LoadSnapshot(JsonSerializer.SerializeToElement(disconnected),acceptGraphics:true);
            if(form.directoryGames.Items.Count!=0||form.directoryRows.Count!=0)
                throw new Exception("An authoritative disconnected snapshot left the old server's rows visible.");
        });

        Check("cancelled-directory-refresh-does-not-block-replacement",form=>{
            form.directoryAvailable=true;form.ShowDirectory(Listing(),gameId);
            var oldResponse=new TaskCompletionSource<JsonElement>();
            Task obsolete=form.RefreshDirectoryCore(true,_=>oldResponse.Task);
            form.SetBusy(true);form.SetBusy(false);
            bool fetched=false;
            AwaitUi(form.RefreshDirectoryCore(false,_=>{fetched=true;return Task.FromResult(Listing("Fresh test match"));}));
            oldResponse.SetResult(Listing("Obsolete test match"));AwaitUi(obsolete);
            if(!fetched||form.directoryRows.Single().GetProperty("title").GetString()!="Fresh test match")
                throw new Exception("A cancelled but not yet drained backend prevented a fresh directory lookup.");
        });

        Check("failed-room-pipe-creation-leaves-no-active-room",form=>{
            form.internetAvailable=true;form.internetHostChoice.Checked=true;form.testInternetConsent=_=>true;
            form.testInternetChannelFactory=()=>throw new BackendError("internet_channel_failed","");
            try{AwaitUi(form.PlayInternet(true));throw new Exception("The pipe creation failure was ignored.");}
            catch(BackendError error)when(error.Code=="internet_channel_failed"){}
            if(form.internetOperation||form.publicRoomOperation||form.internetEnd.Visible)
                throw new Exception("The launcher showed a room even though its private channel could not be created.");
        });

        Check("directory-browsing-does-not-advertise-unsupported-joining",form=>{
            form.directoryAvailable=true;form.internetAvailable=false;form.tabs.SelectedTab=form.directoryPage;
            form.ShowDirectory(Listing(),gameId);Application.DoEvents();form.UpdateDirectoryControls();
            if(form.directoryJoin.Enabled||!form.directoryRefresh.Enabled)
                throw new Exception("A package without Internet play support offered to join the selected listing.");
        });

        if(failures.Count>0)throw new Exception(string.Join(Environment.NewLine,failures));
    }
}
