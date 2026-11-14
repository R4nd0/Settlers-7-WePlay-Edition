namespace S7Desktop;

internal sealed partial class MainForm
{
    private FlowLayoutPanel lanActionBar=null!;
    private bool updateCheckIncomplete;
    private CheckBox graphicsQualityDetails=null!,graphicsEffectDetails=null!;
    private int maintenanceCount;
    private readonly LinkLabel maintenanceNotice=new() {AutoSize=true,MaximumSize=new Size(840,0),Visible=false,Margin=new Padding(0,2,0,4)};
    private Button maintenanceCleanup=null!;
    private Button graphicsRecommended=null!,graphicsMaximum=null!;
    private bool modernGraphicsAvailable;
    private readonly Label graphicsPresetNotice=new() {AutoSize=true,MaximumSize=new Size(840,0),ForeColor=SystemColors.GrayText};
    private void UpdateGraphicsPresets()
    {
        if(graphicsRecommended==null)return;
        graphicsRecommended.Enabled=!busy&&modernGraphicsAvailable;
        graphicsMaximum.Enabled=!busy&&modernGraphicsAvailable&&MaximumGraphicsAvailable;
        graphicsMaximum.Visible=!singlePlayerOnly||MaximumGraphicsAvailable;
        graphicsPresetNotice.Visible=!modernGraphicsAvailable||!midsummerToneAvailable;
        graphicsPresetNotice.Text=Lang.T(!modernGraphicsAvailable?"graphicsModernUnavailable":"graphicsMaximumUnavailable");
    }
    private void UpdateMaintenanceNotice()
    {
        maintenanceNotice.Visible=maintenanceCount>0;
        maintenanceNotice.Text=maintenanceCount>0?string.Format(Lang.T("maintenancePending"),maintenanceCount):"";
        maintenanceNotice.AccessibleName=Lang.T("openMaintenance");
    }
    private static void DockActions(Control host,Control actions)
    {
        actions.Dock=DockStyle.Bottom;actions.Padding=new Padding(14,2,14,2);
        // Dock bottom before the fill sibling consumes the remaining space.
        host.Controls.Add(actions);
    }
    private TableLayoutPanel GraphicsSection(TableLayoutPanel parent,string title)
    {
        var group=new GroupBox {AutoSize=true,Dock=DockStyle.Top,Padding=new Padding(8,16,8,6),Margin=new Padding(0,4,0,10)};
        Bind(group,title);Add(parent,group);return Stack(group);
    }
    private TableLayoutPanel GraphicsDetails(TableLayoutPanel parent,string title,out CheckBox disclosure)
    {
        disclosure=new CheckBox {AutoSize=true,Margin=new Padding(0,6,0,6)};Bind(disclosure,title);Add(parent,disclosure);
        var body=new TableLayoutPanel {AutoSize=true,Dock=DockStyle.Top,ColumnCount=1,Visible=false};
        body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));Add(parent,body);
        var toggle=disclosure;toggle.CheckedChanged+=(_,_)=>{body.Visible=toggle.Checked;FitMenuHeight();};
        return body;
    }
    private void BuildGraphicsPage()
    {
        var scroll=new Panel {Dock=DockStyle.Fill,AutoScroll=true};graphicsPage.Controls.Add(scroll);var root=Stack(scroll);
        root.Padding=new Padding(14,6,14,2);
        var display=GraphicsSection(root,"graphicsDisplayGroup");
        Add(display,activeRenderer);Add(display,Field("renderer",renderer));Add(display,Field("gpu",gpu));
        Bind(keepDisplay,"keepDisplay");Add(display,keepDisplay);
        var outputs=new TableLayoutPanel {AutoSize=true,Dock=DockStyle.Top,ColumnCount=2};
        outputs.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));outputs.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));
        outputs.Controls.Add(Field("display",monitor),0,0);outputs.Controls.Add(Field("window",window),1,0);
        outputs.Controls.Add(Field("resolution",resolution),0,1);Add(display,outputs);
        var displayInfo=GraphicsDetails(display,"graphicsDisplayInfo",out _);
        Add(displayInfo,Label("rendererInfo"));Add(displayInfo,Label("graphicsModeInfo"));Add(displayInfo,Label("borderlessInfo"));

        var quality=GraphicsSection(root,"graphicsQualityGroup");
        graphicsRecommended=Button("qualityRecommended",()=>{ApplyRecommendedGraphics();return Task.CompletedTask;});
        graphicsMaximum=Button("qualityMaximum",()=>{ApplyRecommendedGraphics();maximumFx.Checked=MaximumGraphicsAvailable;return Task.CompletedTask;});
        Add(quality,Buttons(Button("qualityClear",()=>{ApplyQualityPreset(false);return Task.CompletedTask;}),
            Button("qualityDetail",()=>{ApplyQualityPreset(true);return Task.CompletedTask;}),
            graphicsRecommended,graphicsMaximum));
        Add(quality,graphicsPresetNotice);
        Add(quality,Label("qualityRecommendedInfo"));
        var custom=GraphicsDetails(quality,"graphicsCustomQuality",out graphicsQualityDetails);
        Add(custom,Label("qualityPresetInfo"));
        var grid=new TableLayoutPanel {AutoSize=true,Dock=DockStyle.Top,ColumnCount=2};
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));
        string[] levels=["ShaderQuality","TextureQuality","ObjectQuality","DistanceCulling","ShadowQuality","WaterQuality","ClutterQuality"];
        for(int i=0;i<levels.Length;i++){var combo=Combo();qualities[levels[i]]=combo;grid.Controls.Add(Field(levels[i],combo),i%2,i/2);}
        Add(custom,grid);
        var effectsRow=new FlowLayoutPanel {AutoSize=true,Dock=DockStyle.Top,WrapContents=true};
        foreach(string key in new[]{"Shadows","SSAO","DepthOfField","SunShafts","Parallax","Antialiasing"})
        {var box=new CheckBox {AutoSize=true,Margin=new Padding(0,8,18,8)};Bind(box,key);effects[key]=box;effectsRow.Controls.Add(box);}
        Add(custom,effectsRow);

        var extras=GraphicsSection(root,"graphicsEffectsGroup");
        var advanced=GraphicsDetails(extras,"graphicsShowEffects",out graphicsEffectDetails);
        foreach(var item in new[]{(anisotropicFiltering,"anisotropicFiltering","anisotropicInfo"),
            (extendedDetail,"extendedDetail","extendedDetailInfo"),(detailedShadows,"detailedShadows","detailedShadowsInfo"),
            (extendedShadowRange,"extendedShadowRange","extendedShadowRangeInfo"),(ultraShadows,"ultraShadows","ultraShadowsInfo"),
            (midsummerTone,"midsummerTone","midsummerToneInfo"),(enhancedAa,"enhancedAa","enhancedAaInfo"),
            (contactShadows,"contactShadows","contactShadowsInfo"),(maximumFx,"maximumFx","maximumFxInfo")})
        {Bind(item.Item1,item.Item2);Add(advanced,item.Item1);Add(advanced,Label(item.Item3));}
        Bind(simple,"simple");Add(advanced,simple);
        var footer=new TableLayoutPanel {AutoSize=true,Dock=DockStyle.Bottom,ColumnCount=1,Padding=new Padding(14,2,14,2)};
        footer.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        graphicsDiscard=Button("graphicsDiscard",()=>{DiscardGraphicsDraft();return Task.CompletedTask;});
        graphicsSave=Button("save",SaveGraphics,true);
        Add(footer,graphicsDraftNotice);Add(footer,Buttons(graphicsSave,graphicsDiscard));
        graphicsPage.Controls.Add(footer);
        monitor.SelectedIndexChanged+=(_,_)=>{if(!loading)UpdateResolutions();};
        window.SelectedIndexChanged+=(_,_)=>{if(!loading)UpdateResolutions();};
        keepDisplay.CheckedChanged+=(_,_)=>DisplayEnabled();renderer.SelectedIndexChanged+=(_,_)=>DisplayEnabled();
        resolution.SelectedIndexChanged+=(_,_)=>DisplayEnabled();
    }
}
