using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private static void TestLauncherPresentation(JsonElement fixture)
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

        Check("language-change-keeps-current-failure-visible",form=>{
            form.ShowError(new BackendError("renderer_unsupported","Synthetic graphics failure"));
            form.language.SelectedIndex=Lang.Current=="de"?1:0;
            if(form.status.Text!=Lang.T("renderer_unsupported"))
                throw new Exception("Changing language made a real error look like Ready.");
        });

        Check("switching-renderer-back-retains-compatible-draft-AA",form=>{
            var capable=JsonNode.Parse(fixture.GetRawText())!.AsObject();
            capable["midsummer_tone_supported"]=true;
            capable["choice"]!["enhanced_aa"]=true;
            form.LoadSnapshot(JsonSerializer.SerializeToElement(capable),acceptGraphics:true);
            form.renderer.SelectedIndex=3;
            if(form.AntialiasingMode=="smaa")
                throw new Exception("The native renderer exposed unsupported SMAA.");
            form.renderer.SelectedIndex=0;
            if(form.AntialiasingMode!="smaa"||form.GraphicsDirty)
                throw new Exception("Previewing the native renderer erased the saved SMAA selection.");
        });

        Check("discard-restores-last-saved-windowed-size",form=>{
            form.window.SelectedIndex=0;form.resolution.SelectedIndex=0;
            form.window.SelectedIndex=1;form.AcceptGraphicsDraft();
            form.window.SelectedIndex=0;form.resolution.SelectedIndex=1;
            form.DiscardGraphicsDraft();
            form.window.SelectedIndex=0;
            if((form.resolution.SelectedItem as Item)?.Key!="1280x720")
                throw new Exception("Discard left the unsaved windowed size in the per-display cache.");
        });

        Check("expired-directory-row-cannot-launch-join",form=>{
            form.directoryAvailable=true;form.tabs.SelectedTab=form.directoryPage;
            var listing=JsonSerializer.SerializeToElement(new {connected=true,games=new[]{
                new {id="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",title="Expired test match",state="open",compatible=true,
                    free_slots=3,version="rc4.10",players=1,capacity=4,expires_at=DateTimeOffset.UtcNow.ToUnixTimeSeconds()-60}
            }});
            form.ShowDirectory(listing,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");Application.DoEvents();form.UpdateDirectoryControls();
            if(form.directoryJoin.Enabled)
                throw new Exception("The launcher offered an Internet connection to a match already expired in the directory.");
        });

        Check("gold-help-does-not-advertise-unavailable-History-routes",form=>{
            var gold=JsonNode.Parse(fixture.GetRawText())!.AsObject();gold["single_player_only"]=true;
            form.LoadSnapshot(JsonSerializer.SerializeToElement(gold),acceptGraphics:true);
            form.tabs.SelectedTab=form.helpPage;Application.DoEvents();
            if(form.labels.Any(entry=>entry.Value is "roomGuide" or "invitationGuide"&&entry.Key.Visible))
                throw new Exception("Gold Help linked to multiplayer modes that are absent on Gold.");
            if(!form.labels.Any(entry=>entry.Value=="verify"&&entry.Key.Visible))
                throw new Exception("Game diagnostics should still be available on Gold.");
        });

        if(failures.Count>0)throw new Exception(string.Join(Environment.NewLine,failures));
    }
}
