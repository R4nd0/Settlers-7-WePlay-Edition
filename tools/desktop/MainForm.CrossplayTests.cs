using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private void TestCrossplayEdition(JsonElement history,string folder,string language)
    {
        var gold=JsonNode.Parse(history.GetRawText())!.AsObject();
        gold["single_player_only"]=true;gold["edition"]="gold";gold["crossplay_available"]=true;
        gold["internet_available"]=false;
        using(var value=JsonDocument.Parse(gold.ToJsonString()))LoadSnapshot(value.RootElement,acceptGraphics:true);
        SetBusy(false);ShowMain();Application.DoEvents();
        if(tabs.TabPages.Count!=5||tabs.TabPages.Contains(lanPage)||tabs.TabPages.Contains(directoryPage)
                ||!tabs.TabPages.Contains(internetPage)||playModes.Count(x=>x.Card.Visible)!=2)
            throw new Exception("Qualified Gold must expose solo and mixed host only");
        CaptureMixed("gold-mixed-main");
        tabs.SelectedTab=internetPage;UpdateInternetControls();Application.DoEvents();
        if(!MixedInternet||!internetHostChoice.Checked||internetGuestChoice.Visible||crossplayKindField.Visible
                ||!crossplayHistoryField.Visible||internetCreate.Enabled)
            throw new Exception("Mixed Gold host must require an explicit History edition");
        crossplayHistory.SelectedIndex=1;Application.DoEvents();
        if(!internetCreate.Enabled||internetJoinButton.Visible||!internetJoin.UseSystemPasswordChar)
            throw new Exception("Mixed Gold host route or private invitation guard failed");
        ApplyLanguage();Application.DoEvents();
        if(crossplayHistory.SelectedIndex!=1||!MixedInternet||renderer.Items.Count!=4)
            throw new Exception("Language change altered mixed edition choice or Gold renderer");
        CaptureMixed("gold-mixed-host");
        var guest=JsonNode.Parse(history.GetRawText())!.AsObject();guest["crossplay_available"]=true;
        using(var value=JsonDocument.Parse(guest.ToJsonString()))LoadSnapshot(value.RootElement,acceptGraphics:true);
        SetBusy(false);tabs.SelectedTab=internetPage;internetSessionKind.SelectedIndex=1;
        UpdateInternetControls();Application.DoEvents();
        if(!internetGuestChoice.Checked||internetHostChoice.Visible||crossplayHistoryField.Visible
                ||!crossplayKindField.Visible||!internetGuestPanel.Visible||internetCreate.Visible)
            throw new Exception("History mixed route must be guest-only");
        internetJoin.Text="weplayx1.synthetic-fixture";
        if(!internetJoinButton.Enabled)throw new Exception("Mixed invitation did not enable History join");
        string shared=Community.InvitationMessage(internetJoin.Text,language);
        if(Community.InvitationCode(shared)!=internetJoin.Text||shared.Contains("weplay1.")
                ||shared.Contains("→ select the creator's match"))
            throw new Exception("Mixed private share route is not edition-specific");
        CaptureMixed("history-mixed-guest");
        internetJoin.Clear();gold["crossplay_available"]=false;
        using(var value=JsonDocument.Parse(gold.ToJsonString()))LoadSnapshot(value.RootElement,acceptGraphics:true);
        SetBusy(false);ShowMain();Application.DoEvents();
        if(tabs.TabPages.Contains(internetPage)||playModes.Count(x=>x.Card.Visible)!=1)
            throw new Exception("Removing mixed package capability did not hide Gold network routes");
        LoadSnapshot(history,acceptGraphics:true);internetHostChoice.Checked=true;crossplayHistory.SelectedIndex=-1;
        SetBusy(false);tabs.SelectedTab=homePage;Application.DoEvents();

        void CaptureMixed(string name)
        {
            using var image=new Bitmap(Width,Height);DrawToBitmap(image,new Rectangle(0,0,Width,Height));
            image.Save(Path.Combine(folder,language+"-"+name+".png"));
        }
    }
}
