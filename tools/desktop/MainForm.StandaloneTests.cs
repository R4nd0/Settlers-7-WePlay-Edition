using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private void TestStandaloneEdition(JsonElement history,string folder,string language)
    {
        var gold=JsonNode.Parse(history.GetRawText())!.AsObject();
        gold["single_player_only"]=true;gold["edition"]="gold";
        using var value=JsonDocument.Parse(gold.ToJsonString());
        LoadSnapshot(value.RootElement,acceptGraphics:true);SetBusy(false);ShowMain();Application.DoEvents();
        if(tabs.TabPages.Count!=4||tabs.TabPages.Contains(lanPage)||!tabs.TabPages.Contains(graphicsPage)||
           tabs.TabPages.Contains(internetPage)||tabs.TabPages.Contains(directoryPage)||!openGraphics.Visible)
            throw new Exception("Gold exposes unsupported History routes");
        if(renderer.Items.Count!=2)throw new Exception("Gold exposes unsupported native renderer");
        if(graphicsMaximum.Visible||maximumFx.Visible)throw new Exception("Gold exposes unqualified Maximum combination");
        if(playModes.Count(x=>x.Card.Visible)!=1||!playModes.Single(x=>x.Card.Mode==PlayMode.Solo).Card.Visible)
            throw new Exception("Gold must expose its local single-player route");
        if(labels.Any(x=>x.Value=="modeConsentInfo"&&x.Key.Visible))throw new Exception("Gold exposes irrelevant Internet instructions");
        using(var image=new Bitmap(Width,Height))
        {DrawToBitmap(image,new Rectangle(0,0,Width,Height));image.Save(Path.Combine(folder,language+"-gold-main.png"));}
        gold["maximum_fx_supported"]=true;gold["midsummer_tone_supported"]=true;gold["visual_detail_supported"]=true;
        using(var fullhd=JsonDocument.Parse(gold.ToJsonString()))LoadSnapshot(fullhd.RootElement,acceptGraphics:true);
        tabs.SelectedTab=graphicsPage;Application.DoEvents();DisplayEnabled();
        if(graphicsMaximum.Visible||maximumFx.Visible||!extendedDetail.Enabled||!enhancedAa.Enabled)
            throw new Exception("Gold must expose working effects and omit defective Maximum");
        graphicsEffectDetails.Checked=true;Application.DoEvents();
        using(var image=new Bitmap(Width,Height))
        {DrawToBitmap(image,new Rectangle(0,0,Width,Height));image.Save(Path.Combine(folder,language+"-gold-graphics.png"));}
        keepDisplay.Checked=true;Application.DoEvents();
        if(graphicsMaximum.Visible||maximumFx.Visible)throw new Exception("Gold exposes Maximum for unknown output size");
        keepDisplay.Checked=false;Application.DoEvents();
        ApplyLanguage();
        if(renderer.Items.Count!=2)throw new Exception("Language refresh restores Gold's unsupported native renderer");
        LoadSnapshot(history,acceptGraphics:true);SetBusy(false);tabs.SelectedTab=homePage;Application.DoEvents();
        if(tabs.TabPages.Count!=7||playModes.Any(x=>!x.Card.Visible)||!openGraphics.Visible)
            throw new Exception("History routes did not recover after Gold snapshot");
    }
}
