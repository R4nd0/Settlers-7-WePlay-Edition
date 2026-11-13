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
        if(tabs.TabPages.Count!=3||tabs.TabPages.Contains(lanPage)||tabs.TabPages.Contains(graphicsPage)||
           tabs.TabPages.Contains(internetPage)||tabs.TabPages.Contains(directoryPage)||openGraphics.Visible)
            throw new Exception("Gold exposes unsupported History routes");
        if(playModes.Count(x=>x.Card.Visible)!=1||!playModes.Single(x=>x.Card.Mode==PlayMode.Solo).Card.Visible)
            throw new Exception("Gold must expose its local single-player route");
        if(labels.Any(x=>x.Value=="modeConsentInfo"&&x.Key.Visible))throw new Exception("Gold exposes irrelevant Internet instructions");
        using(var image=new Bitmap(Width,Height))
        {DrawToBitmap(image,new Rectangle(0,0,Width,Height));image.Save(Path.Combine(folder,language+"-gold-main.png"));}
        LoadSnapshot(history,acceptGraphics:true);SetBusy(false);Application.DoEvents();
        if(tabs.TabPages.Count!=7||playModes.Any(x=>!x.Card.Visible)||!openGraphics.Visible)
            throw new Exception("History routes did not recover after Gold snapshot");
    }
}
