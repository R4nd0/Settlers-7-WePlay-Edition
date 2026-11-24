using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private void TestNativeGraphicsUi(string folder,string language)
    {
        var previous=snapshot.Clone();
        var fixture=JsonNode.Parse(previous.GetRawText())!.AsObject();
        fixture["native_graphics_features"]=new JsonArray("supersampling","improved_water",
            "improved_terrain","improved_vegetation","stable_shadows");
        fixture["native_graphics_scales"]=new JsonArray(100,150,200);
        fixture["quality"]!["ShaderQuality"]=2;
        fixture["quality"]!["WaterQuality"]=2;
        foreach(string name in new[]{"midsummer_tone","enhanced_aa","contact_shadows","maximum_fx"})
            fixture["choice"]![name]=false;
        fixture["choice"]!["native_graphics"]=new JsonObject {
            ["render_scale"]=100,["improved_water"]=false,["improved_terrain"]=false,
            ["improved_vegetation"]=false,["stable_shadows"]=false};
        try
        {
            LoadSnapshot(JsonSerializer.SerializeToElement(fixture),acceptGraphics:true);
            setup.Visible=false;tabs.Visible=true;tabs.SelectedTab=graphicsPage;
            for(int i=0;i<renderer.Items.Count;i++)if(renderer.Items[i] is Item {Key:"dxvk"})renderer.SelectedIndex=i;
            keepDisplay.Checked=false;monitor.SelectedIndex=0;window.SelectedIndex=0;
            if(resolution.Items.Count==0)throw new Exception("Native quality needs an explicit output fixture");
            resolution.SelectedIndex=0;nativeRenderScale.SelectedIndex=2;
            if(!nativeRenderScale.Enabled||!nativeGraphicsGroup!.Visible||NativeGraphicsIssue!=null)
                throw new Exception("Qualified native SSAA fixture is not selectable");
            using(var request=JsonDocument.Parse(JsonSerializer.Serialize(GraphicsRequest())))
                if(request.RootElement.GetProperty("choice").GetProperty("native_graphics").GetProperty("render_scale").GetInt32()!=200)
                    throw new Exception("Native scale did not survive the desktop request");
            if(!GraphicsDirty)throw new Exception("Native edit bypassed unsaved-change tracking");
            nativeGraphicsScales.Remove(200);
            if(NativeGraphicsIssue!="native_graphics_unavailable")throw new Exception("Unqualified scale is silently accepted");
            nativeGraphicsScales.Add(200);
            nativeWater.Checked=true;
            if(!nativeWater.Enabled||NativeGraphicsIssue!=null)throw new Exception("Qualified native material fixture is disabled");
            Application.DoEvents();
            var scroll=(Panel)graphicsPage.Controls[0];scroll.ScrollControlIntoView(nativeGraphicsGroup);
            using(var image=new Bitmap(Width,Height))
            {DrawToBitmap(image,new Rectangle(0,0,Width,Height));image.Save(Path.Combine(folder,language+"-native-graphics-fixture.png"));}
            nativeGraphicsFeatures.Remove("improved_water");
            if(NativeGraphicsIssue!="native_graphics_unavailable")throw new Exception("Unqualified material is silently accepted");
            nativeWater.Checked=false;keepDisplay.Checked=true;
            if(NativeGraphicsIssue!="native_graphics_display_required")throw new Exception("Native SSAA changes an implicit display");
            nativeRenderScale.SelectedIndex=0;nativeGraphicsFeatures.Clear();UpdateNativeGraphics();
            if(NativeGraphicsActive||NativeGraphicsIssue!=null||nativeGraphicsGroup.Visible)
                throw new Exception("Inactive native graphics disturbs legacy settings");
        }
        finally {LoadSnapshot(previous,acceptGraphics:true);}
    }
}
