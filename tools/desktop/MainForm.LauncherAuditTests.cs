using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private static void TestLauncherAudit(JsonElement fixture)
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
            finally
            {
                Lang.Set(locale,false);
                form.gameRunning=false;form.busy=false;form.savedGraphicsDraft=null;form.Close();
            }
        }

        Check("graphics-discard-preserves-LAN-port",form=>{
            form.testUiBackend=(action,data)=>Task.FromResult(JsonSerializer.SerializeToElement(new {lan_discovery_port=19112}));
            AwaitUi(form.SaveLanDiscoveryPort(19112));
            form.lanDiscoveryPort.Value=19113;
            form.qualities["TextureQuality"].SelectedIndex=2;
            form.DiscardGraphicsDraft();
            if(form.GraphicsDirty||form.savedLanDiscoveryPort!=19112||form.lanDiscoveryPort.Value!=19113)
                throw new Exception("Discard reset the saved or pending LAN discovery port.");
        });

        Check("language-keeps-connected-directory",form=>{
            var available=JsonNode.Parse(fixture.GetRawText())!.AsObject();
            available["directory_available"]=true;
            form.LoadSnapshot(JsonSerializer.SerializeToElement(available),acceptGraphics:true);
            form.ShowDirectory(JsonSerializer.SerializeToElement(new {connected=true,games=Array.Empty<object>()}));
            form.language.SelectedIndex=Lang.Current=="de"?1:0;
            if(!form.directoryConnected||!form.directoryRefresh.Enabled)
                throw new Exception("Relocalization reverted a successful directory response.");
        });

        Check("language-translates-loaded-directory-rows",form=>{
            using var listing=JsonDocument.Parse("""{"connected":true,"games":[{"id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","title":"Fixture match","state":"open","free_slots":3,"compatible":true,"version":"rc4.10","players":1,"capacity":4}]}""");
            form.ShowDirectory(listing.RootElement,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
            form.language.SelectedIndex=Lang.Current=="de"?1:0;
            if(form.directoryGames.Items[0].SubItems[1].Text!=Lang.T("directoryOpen"))
                throw new Exception("An already loaded game kept its former-language status.");
        });

        Check("language-translates-tray-menu",form=>{
            form.language.SelectedIndex=Lang.Current=="de"?1:0;
            if(form.tray.ContextMenuStrip!.Items[0].Text!=Lang.T("show")||
                form.tray.ContextMenuStrip.Items[1].Text!=Lang.T("exit"))
                throw new Exception("The tray menu kept its former-language commands.");
        });

        Check("cancel-is-queued-once",form=>{
            int requests=0;
            form.SetBusy(true);
            form.RequestCancellation(()=>requests++);
            form.RequestCancellation(()=>requests++);
            if(requests!=1||!form.cancelRequested)
                throw new Exception("A second cancellation reached the same active operation.");
        });

        Check("gold-game-finish-restores-edition-copy",form=>{
            var gold=JsonNode.Parse(fixture.GetRawText())!.AsObject();
            gold["single_player_only"]=true;gold["crossplay_available"]=true;
            form.LoadSnapshot(JsonSerializer.SerializeToElement(gold),acceptGraphics:true);
            form.Receive(JsonSerializer.SerializeToElement(new {kind="game_started"}));
            form.Receive(JsonSerializer.SerializeToElement(new {kind="game_finished"}));
            if(form.homeInfo.Text!=Lang.T("goldMixedIntro"))
                throw new Exception("The finished Gold game displayed generic History Edition instructions.");
        });

        Check("failed-game-restores-idle-title",form=>{
            form.testUiBackend=(action,data)=>Task.FromResult(JsonSerializer.SerializeToElement(new {pending=false}));
            AwaitUi(form.Guard(()=>{
                form.Receive(JsonSerializer.SerializeToElement(new {kind="game_started"}));
                return Task.FromException(new BackendError("operation_failed","Synthetic game exit"));
            }));
            if(form.gameRunning||form.homeTitle.Text!=Lang.T("playTitle"))
                throw new Exception("The homepage still said the game was running after it failed.");
        });

        Check("restoring-visible-window-preserves-maximized-state",form=>{
            form.WindowState=FormWindowState.Maximized;
            form.RestoreWindow();
            if(form.WindowState!=FormWindowState.Maximized)
                throw new Exception("Restoring focus shrank a maximized launcher.");
        });

        Check("graphics-save-requires-valid-manual-display",form=>{
            var unavailable=JsonNode.Parse(fixture.GetRawText())!.AsObject();
            unavailable["displays"]=new JsonArray();
            form.LoadSnapshot(JsonSerializer.SerializeToElement(unavailable),acceptGraphics:true);
            form.tabs.SelectedTab=form.graphicsPage;
            if(form.keepDisplay.Checked||form.graphicsSave.Enabled||form.resolution.Enabled||!form.graphicsDisplayUnavailable.Visible)
                throw new Exception("Invalid display controls advertised a save that the launcher would refuse.");
            form.keepDisplay.Checked=true;
            if(!form.graphicsSave.Enabled||form.graphicsDisplayUnavailable.Visible)
                throw new Exception("Keeping the current display settings should allow saving.");
        });

        Check("graphics-save-is-independent-of-unfinished-player-name",form=>{
            form.player.Text="";
            string? name=JsonSerializer.SerializeToElement(form.GraphicsRequest()).GetProperty("player_name").GetString();
            if(name!=fixture.GetProperty("player_name").GetString())
                throw new Exception("Graphics save included an uncommitted LAN name.");
            form.ApplySettingsResult(fixture);
            if(form.player.Text!="")
                throw new Exception("Loading a graphics result discarded the unfinished name.");
        });

        if(failures.Count>0)throw new Exception(string.Join(Environment.NewLine,failures));
    }
}
