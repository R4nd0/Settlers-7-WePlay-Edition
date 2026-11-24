using System.Text.Json;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private readonly ComboBox nativeRenderScale=Combo();
    private readonly CheckBox nativeWater=new() {AutoSize=true};
    private readonly CheckBox nativeTerrain=new() {AutoSize=true};
    private readonly CheckBox nativeVegetation=new() {AutoSize=true};
    private readonly CheckBox nativeShadows=new() {AutoSize=true};
    private readonly Label nativeResolutionInfo=new() {AutoSize=true,MaximumSize=new Size(840,0),ForeColor=SystemColors.GrayText};
    private Control? nativeGraphicsGroup;
    private readonly HashSet<string> nativeGraphicsFeatures=new(StringComparer.Ordinal);
    private readonly HashSet<int> nativeGraphicsScales=[100];
    private static readonly int[] NativeScales=[100,150,200];
    private (CheckBox Box,string Feature,string Label)[] NativeFlags=>[
        (nativeWater,"improved_water","nativeWater"),(nativeTerrain,"improved_terrain","nativeTerrain"),
        (nativeVegetation,"improved_vegetation","nativeVegetation"),(nativeShadows,"stable_shadows","nativeShadows")];
    private int NativeRenderPercent=>NativeScales[Math.Clamp(nativeRenderScale.SelectedIndex,0,NativeScales.Length-1)];
    private bool NativeGraphicsActive=>NativeRenderPercent!=100||NativeFlags.Any(x=>x.Box.Checked);

    private Dictionary<string,object> NativeGraphicsValues()
    {
        var result=new Dictionary<string,object> { ["render_scale"]=NativeRenderPercent };
        foreach(var item in NativeFlags)result[item.Feature]=item.Box.Checked;
        return result;
    }

    private void BuildNativeGraphics(TableLayoutPanel parent)
    {
        var body=GraphicsSection(parent,"nativeGraphics");nativeGraphicsGroup=body.Parent!;
        Add(body,Field("nativeRenderScale",nativeRenderScale));
        Add(body,Label("nativeRenderScaleInfo"));Add(body,nativeResolutionInfo);
        foreach(var item in NativeFlags){Bind(item.Box,item.Label);Add(body,item.Box);}
        ApplyNativeGraphicsLanguage();UpdateNativeGraphics();
        nativeRenderScale.SelectedIndexChanged+=(_,_)=>{if(!loading){DisplayEnabled();UpdateGraphicsDraft();}};
        foreach(var item in NativeFlags)item.Box.CheckedChanged+=(_,_)=>{if(!loading){DisplayEnabled();UpdateGraphicsDraft();}};
    }

    private void ApplyNativeGraphicsLanguage()
    {
        int previous=Math.Clamp(nativeRenderScale.SelectedIndex,0,2);
        nativeRenderScale.Items.Clear();nativeRenderScale.Items.AddRange([
            Lang.T("nativeScaleOff"),Lang.T("nativeScale150"),Lang.T("nativeScale200")]);
        nativeRenderScale.SelectedIndex=previous;
    }

    private void LoadNativeGraphics(JsonElement value,bool replaceFields)
    {
        nativeGraphicsFeatures.Clear();
        nativeGraphicsScales.Clear();nativeGraphicsScales.Add(100);
        if(value.TryGetProperty("native_graphics_scales",out var scales)&&scales.ValueKind==JsonValueKind.Array)
            foreach(var scale in scales.EnumerateArray())
                if(scale.TryGetInt32(out int percent)&&NativeScales.Contains(percent))nativeGraphicsScales.Add(percent);
        if(value.TryGetProperty("native_graphics_features",out var features)&&features.ValueKind==JsonValueKind.Array)
            foreach(var feature in features.EnumerateArray())
                if(feature.ValueKind==JsonValueKind.String&&feature.GetString() is string name)nativeGraphicsFeatures.Add(name);
        if(replaceFields)
        {
            var selected=value.GetProperty("choice");
            bool present=selected.TryGetProperty("native_graphics",out var native)&&native.ValueKind==JsonValueKind.Object;
            int scale=present&&native.TryGetProperty("render_scale",out var number)&&number.TryGetInt32(out int percentage)?percentage:100;
            nativeRenderScale.SelectedIndex=Math.Max(0,Array.IndexOf(NativeScales,scale));
            foreach(var item in NativeFlags)item.Box.Checked=present&&native.TryGetProperty(item.Feature,out var enabled)
                &&enabled.ValueKind==JsonValueKind.True;
        }
        UpdateNativeGraphics();
    }

    private string? NativeGraphicsIssue
    {
        get
        {
            if(!NativeGraphicsActive)return null;
            if((NativeRenderPercent!=100&&!nativeGraphicsFeatures.Contains("supersampling"))
                ||NativeFlags.Any(x=>x.Box.Checked&&!nativeGraphicsFeatures.Contains(x.Feature)))return "native_graphics_unavailable";
            if(!nativeGraphicsScales.Contains(NativeRenderPercent))return "native_graphics_unavailable";
            if(RendererMode!="dxvk")return "native_graphics_renderer_required";
            if(midsummerTone.Checked||contactShadows.Checked||maximumFx.Checked||AntialiasingMode=="smaa")
                return "native_graphics_effect_conflict";
            if((nativeWater.Checked||nativeTerrain.Checked||nativeVegetation.Checked)
                &&(!qualities.TryGetValue("ShaderQuality",out var shaders)||shaders.SelectedIndex!=2
                   ||(nativeWater.Checked&&(!qualities.TryGetValue("WaterQuality",out var water)||water.SelectedIndex!=2))))
                return "native_graphics_high_quality_required";
            if(keepDisplay.Checked||resolution.SelectedItem is not Item selected)return "native_graphics_display_required";
            var size=selected.Key.Split('x');
            if(size.Length!=2||!int.TryParse(size[0],out int width)||!int.TryParse(size[1],out int height)
                ||width<800||height<600)return "native_graphics_resolution_invalid";
            if(width*NativeRenderPercent%100!=0||height*NativeRenderPercent%100!=0)return "native_graphics_resolution_invalid";
            if(width*NativeRenderPercent/100>3840||height*NativeRenderPercent/100>2160)return "native_graphics_resolution_limit";
            return null;
        }
    }

    private void UpdateNativeGraphics()
    {
        if(nativeGraphicsGroup==null)return;
        nativeGraphicsGroup.Visible=nativeGraphicsFeatures.Count>0||NativeGraphicsActive;
        nativeRenderScale.Enabled=!busy&&(nativeGraphicsFeatures.Contains("supersampling")||NativeRenderPercent!=100);
        foreach(var item in NativeFlags)item.Box.Enabled=!busy&&(nativeGraphicsFeatures.Contains(item.Feature)||item.Box.Checked);
        string? issue=NativeGraphicsIssue;
        nativeResolutionInfo.Text=issue!=null?Lang.T(issue):Lang.T("nativeGraphicsInfo");
        if(issue==null&&NativeGraphicsActive&&!keepDisplay.Checked&&resolution.SelectedItem is Item selected)
        {
            var size=selected.Key.Split('x');
            if(size.Length==2&&int.TryParse(size[0],out int width)&&int.TryParse(size[1],out int height))
                nativeResolutionInfo.Text=string.Format(Lang.T("nativeRenderDimensions"),
                    width*NativeRenderPercent/100,height*NativeRenderPercent/100,
                    NativeRenderPercent*NativeRenderPercent/10000.0);
        }
    }
}
