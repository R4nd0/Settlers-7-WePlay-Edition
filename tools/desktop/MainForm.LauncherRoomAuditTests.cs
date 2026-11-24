using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private static void TestLauncherRooms(JsonElement fixture)
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

        Check("direct-host-requires-an-endpoint-before-consent",form=>{
            form.internetAvailable=true;form.tabs.SelectedTab=form.internetPage;
            form.internetHostChoice.Checked=true;form.internetAdvanced.Checked=true;
            form.internetMode.SelectedIndex=1;
            form.UpdateInternetControls();
            if(form.internetCreate.Enabled)
                throw new Exception("A direct session can start with no endpoint.");
            form.internetEndpoint.Text="tcp://198.51.100.41:20011";
            if(!form.internetCreate.Enabled)
                throw new Exception("Adding an endpoint did not enable Create.");
            form.internetEndpoint.Clear();form.internetMode.SelectedIndex=0;
            if(!form.internetCreate.Enabled)
                throw new Exception("Automatic room hosting does not require an endpoint.");
        });

        Check("invalid-invitation-does-not-enable-join",form=>{
            form.internetAvailable=true;form.tabs.SelectedTab=form.internetPage;form.internetGuestChoice.Checked=true;
            form.internetJoin.Text="incomplete invitation";
            if(form.internetJoinButton.Enabled)
                throw new Exception("The launcher advertises Join for text that is not an invitation.");
            form.internetJoin.Text="weplay1.synthetic";
            if(!form.internetJoinButton.Enabled)
                throw new Exception("A formatted invitation should enable Join.");
        });

        Check("failed-room-connection-preserves-invitation-draft",form=>{
            form.internetAvailable=true;form.tabs.SelectedTab=form.internetPage;form.internetGuestChoice.Checked=true;
            const string invitation="weplay1.synthetic";
            form.internetJoin.Text=invitation;
            form.BeginInternetOperation(false,false,false);
            form.EndInternetOperation();
            if(form.internetJoin.Text!=invitation||form.internetOperation)
                throw new Exception("A failed join discarded the invitation needed to retry.");
        });

        Check("copying-a-markdown-code-block-can-paste-directly",_=>{
            const string invitation="weplayx1.synthetic";
            if(Community.InvitationCode("```\n"+invitation+"\n```")!=invitation||
                Community.InvitationCode("`"+invitation+"`")!=invitation)
                throw new Exception("An invitation copied from a Markdown code block was refused.");
            try{Community.InvitationCode("unrelated message "+invitation);throw new Exception("Invitation parsing should not scan arbitrary messages.");}
            catch(BackendError error)when(error.Code=="internet_invitation_invalid"){}
        });

        Check("graphics-probe-replaces-progress-with-an-outcome",form=>{
            form.status.Text=Lang.T("phase_graphics_probe");
            form.testUiBackend=(action,data)=>Task.FromResult(action=="snapshot"?fixture:JsonSerializer.SerializeToElement(new {}));
            AwaitUi(form.Probe());
            if(form.status.Text!=Lang.T("graphicsProbeComplete"))
                throw new Exception("Graphics status continued to show a check in progress after success.");
            var unsupported=JsonNode.Parse(fixture.GetRawText())!.AsObject();
            unsupported["probe"]!["devices"]=new JsonArray();unsupported["probe"]!["failure"]="unsupported";
            form.testUiBackend=(action,data)=>Task.FromResult(action=="snapshot"?JsonSerializer.SerializeToElement(unsupported):JsonSerializer.SerializeToElement(new {}));
            AwaitUi(form.Probe());
            if(form.status.Text!=Lang.T("graphicsProbeUnconfirmed"))
                throw new Exception("The completed graphics check did not explain unsupported hardware.");
        });

        if(failures.Count>0)throw new Exception(string.Join(Environment.NewLine,failures));
    }
}
