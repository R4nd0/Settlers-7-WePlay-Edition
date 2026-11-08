namespace S7Desktop;

internal sealed partial class MainForm
{
    private readonly List<(PlayModeCard Card,string Summary,string One,string Two,string Action)> playModes=[];
    private TableLayoutPanel modeGrid=null!;

    private Control BuildPlayModes()
    {
        modeGrid=new TableLayoutPanel { Name="play-modes",AutoSize=true,Dock=DockStyle.Top,ColumnCount=2,RowCount=2,Margin=Padding.Empty };
        modeGrid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));modeGrid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));
        modeGrid.RowStyles.Add(new RowStyle(SizeType.AutoSize));modeGrid.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        AddMode(PlayMode.Solo,"startLocalTitle","startLocalInfo","modeSoloFeatures","modeSoloLocal","playButton",()=>Play(false));
        AddMode(PlayMode.Lan,"startLanTitle","startLanInfo","modeLanFind","modeLanNetwork","openLan",()=>{tabs.SelectedTab=lanPage;return Task.CompletedTask;});
        AddMode(PlayMode.PrivateInternet,"startInternetTitle","startInternetInfo","modeEasyTier","modeInvite","openInternet",()=>{tabs.SelectedTab=internetPage;return Task.CompletedTask;});
        AddMode(PlayMode.PublicInternet,"startPublicTitle","startPublicInfo","modeEasyTier","modeDiscord","startPublicAction",()=>{
            tabs.SelectedTab=directoryPage;
            return testing?Task.CompletedTask:RefreshDirectory();
        });
        return modeGrid;
    }
    private void AddMode(PlayMode mode,string title,string summary,string one,string two,string action,Func<Task> open)
    {
        var card=new PlayModeCard(mode){Font=Font,TabIndex=playModes.Count};Bind(card,title);
        card.Click+=async (_,_)=>{if(mode==PlayMode.PublicInternet){if(!busy)await open();}else await Guard(open);};
        playModes.Add((card,summary,one,two,action));
        modeGrid.Controls.Add(card,((int)mode)%2,((int)mode)/2);
    }
    private void ApplyPlayModeLanguage()
    {
        foreach(var entry in playModes)
        {
            entry.Card.Summary=Lang.T(entry.Summary);entry.Card.FeatureOne=Lang.T(entry.One);
            entry.Card.FeatureTwo=Lang.T(entry.Two);entry.Card.ActionLabel=Lang.T(entry.Action);
            entry.Card.RefreshDescription();
        }
    }
    private void ResizePlayModes(Panel viewport)
    {
        // Keep all four routes visible on short screens. Details remain in the
        // accessible description and on the selected mode's page.
        bool compact=viewport.ClientSize.Height<680*Font.Height/17;
        modeGrid.SuspendLayout();
        foreach(var entry in playModes)entry.Card.SetCompact(compact);
        modeGrid.ResumeLayout(true);
    }
}
