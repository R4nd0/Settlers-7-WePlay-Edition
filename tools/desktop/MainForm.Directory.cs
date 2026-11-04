using System.Text.Json;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private TabPage directoryPage=null!;
    private bool directoryAvailable,directoryConnected,publicHostingAvailable,directoryLoadFailed;
    private readonly System.Windows.Forms.Timer directoryTimer=new() {Interval=15000};
    private readonly TextBox directoryPlayer=new() { Dock=DockStyle.Fill,MaxLength=24 };
    private readonly Label directoryNotice=new() { AutoSize=true,MaximumSize=new Size(850,0) };
    private readonly ListView directoryGames=new() { Dock=DockStyle.Top,Height=290,View=View.Details,FullRowSelect=true,MultiSelect=false,HideSelection=false };
    private Button directoryRefresh=null!,directoryJoin=null!,directoryConnect=null!;
    private readonly List<JsonElement> directoryRows=[];
    private string? pendingDirectoryGame;
    private bool publicRoomOperation;
    private readonly Label directoryProgress=new() { AutoSize=true,MaximumSize=new Size(850,0) };
    private Button directoryHost=null!,directoryEnd=null!;
    private void QueueDirectorySwitch(JsonElement result)
    {
        if(result.TryGetProperty("directory_game_id",out var id)&&id.ValueKind==JsonValueKind.String)
            pendingDirectoryGame=id.GetString();
    }

    private TabPage BuildDirectory()
    {
        var page=new TabPage {BackColor=Color.White};Bind(page,"directoryTitle");
        var scroll=new Panel {Dock=DockStyle.Fill,AutoScroll=true};page.Controls.Add(scroll);var stack=Stack(scroll);
        var title=Label("directoryTitle");title.Font=new Font(Font.FontFamily,20,FontStyle.Bold);Add(stack,title);
        Add(stack,Label("directoryIntro"));Add(stack,Field("player",directoryPlayer));
        directoryGames.AccessibleName=Lang.T("directoryTitle");
        directoryGames.Columns.Add("",330);directoryGames.Columns.Add("",155);directoryGames.Columns.Add("",110);directoryGames.Columns.Add("",165);
        directoryGames.SelectedIndexChanged+=(_,_)=>UpdateDirectoryControls();
        directoryRefresh=Button("directoryRefresh",RefreshDirectory);
        directoryJoin=Button("directoryJoin",JoinDirectory,true);
        directoryConnect=Button("directoryConnect",ConnectDirectory);
        Add(stack,directoryGames);Add(stack,Buttons(directoryRefresh,directoryJoin,directoryConnect));Add(stack,directoryNotice);
        Add(stack,Label("directoryHostInfo"));
        directoryHost=Button("directoryHost",async()=>{
            bool publish=publicHostingAvailable?discordPublish.Checked:true;
            if(publish&&!discordConnected)throw new BackendError("discord_not_connected","");
            internetPlayer.Text=directoryPlayer.Text;internetMode.SelectedIndex=0;internetHostChoice.Checked=true;
            await PlayInternet(true,publish:publish,publicLobby:true);
        });
        Add(stack,Buttons(directoryHost));
        Add(stack,directoryProgress);
        directoryEnd=Button("internetLeave",()=>{
            if(internetOperation&&!gameRunning){Backend.Cancel();cancelRequested=true;UpdateDirectoryControls();}
            return Task.CompletedTask;
        });
        Add(stack,Buttons(directoryEnd));
        var discordSection=new GroupBox { AutoSize=true,Dock=DockStyle.Top,Margin=new Padding(0,14,0,8),Font=new Font(Font.FontFamily,11,FontStyle.Bold) };
        Bind(discordSection,"directoryPublisherTitle");var discord=Stack(discordSection);discord.Font=Font;
        Add(discord,Label("directoryPublisherInfo"));Add(discord,discordNotice);
        Bind(discordPublish,"discordPublish");Add(discord,discordPublish);
        Bind(discordOptions,"discordOptions");Add(discord,Buttons(Button("discordFindPlayers",()=>{Community.OpenDiscord();return Task.CompletedTask;}),discordOptions));
        discordOptionsPanel=new TableLayoutPanel { AutoSize=true,Dock=DockStyle.Top,ColumnCount=1 };discordOptionsPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        accessibleFields[discordOptionsPanel]="discordOptions";
        Add(discordOptionsPanel,Label("discordSetupSteps"));
        discordConnect=Button("discordConnect",ConnectDiscord);discordDisconnect=Button("discordDisconnect",DisconnectDiscord);
        Add(discordOptionsPanel,Buttons(discordConnect,discordDisconnect));
        Add(discord,discordOptionsPanel);Add(stack,discordSection);
        discordOptions.CheckedChanged+=(_,_)=>{UpdateInternetControls();FitMenuHeight();if(discordOptions.Checked)scroll.ScrollControlIntoView(discordOptionsPanel);};
        async Task RefreshVisible()
        {
            if(testing||busy||!Visible||WindowState==FormWindowState.Minimized||tabs.SelectedTab!=directoryPage||!directoryAvailable)return;
            await Guard(RefreshDirectory,showErrors:false);
        }
        directoryTimer.Tick+=async(_,_)=>await RefreshVisible();
        tabs.SelectedIndexChanged+=async(_,_)=>await RefreshVisible();
        Shown+=(_,_)=>{if(!testing)directoryTimer.Start();};
        FormClosed+=(_,_)=>directoryTimer.Dispose();
        return page;
    }

    private JsonElement? SelectedDirectoryGame()=>directoryGames.SelectedIndices.Count==1
        &&directoryGames.SelectedIndices[0]<directoryRows.Count?directoryRows[directoryGames.SelectedIndices[0]]:null;
    private static bool DirectoryJoinable(JsonElement row)=>row.GetProperty("compatible").GetBoolean()
        &&row.GetProperty("state").GetString()=="open"&&row.GetProperty("free_slots").GetInt32()>0;

    private void UpdateDirectoryControls()
    {
        if(directoryRefresh==null)return;
        directoryRefresh.Enabled=!busy&&directoryAvailable&&directoryConnected;
        directoryConnect.Enabled=!busy&&directoryAvailable;
        directoryPlayer.Enabled=!busy;
        var selected=SelectedDirectoryGame();
        directoryJoin.Enabled=!busy&&directoryAvailable&&directoryConnected&&selected.HasValue&&DirectoryJoinable(selected.Value);
        string[] headings=["directoryGame","directoryState","directoryPlayers","directoryVersion"];
        for(int i=0;i<headings.Length;i++)directoryGames.Columns[i].Text=Lang.T(headings[i]);
        directoryNotice.Text=Lang.T(!directoryAvailable?"directoryUnavailableInfo":directoryLoadFailed?"directory_unavailable":!directoryConnected?"directoryNotConnected":directoryRows.Count==0?"directoryEmpty":"directoryJoinInfo");
        if(directoryHost!=null)directoryHost.Enabled=!busy&&directoryAvailable&&internetAvailable&&directoryConnected&&(publicHostingAvailable||discordConnected);
        bool active=publicRoomOperation&&internetOperation;
        directoryProgress.Visible=active;
        directoryProgress.Text=Lang.T(cancelRequested?"cancelRequested":gameRunning?"directoryPlaying":
            internetStage=="connecting"?"directoryConnecting":internetStage=="hosting"?"directoryHosting":internetCreating?"directoryHostReady":"directoryGuestReady");
        if(directoryEnd!=null){directoryEnd.Visible=active;directoryEnd.Enabled=active&&!gameRunning&&!cancelRequested;}
    }

    private void ShowDirectory(JsonElement value,string? select=null)
    {
        directoryConnected=value.GetProperty("connected").GetBoolean();directoryRows.Clear();directoryGames.Items.Clear();
        if(value.TryGetProperty("games",out var games))foreach(var row in games.EnumerateArray())
        {
            directoryRows.Add(row.Clone());
            string state=!row.GetProperty("compatible").GetBoolean()?"directoryIncompatible":row.GetProperty("state").GetString()=="started"?"directoryStarted":row.GetProperty("free_slots").GetInt32()==0?"directoryFull":"directoryOpen";
            var item=new ListViewItem([row.GetProperty("title").GetString()!,Lang.T(state),row.GetProperty("players")+" / "+row.GetProperty("capacity"),row.GetProperty("version").GetString()!]);
            directoryGames.Items.Add(item);
            if(row.GetProperty("id").GetString()==select)item.Selected=true;
        }
        UpdateDirectoryControls();
    }

    private async Task RefreshDirectory()
    {
        if(!directoryAvailable){UpdateDirectoryControls();return;}
        var selected=SelectedDirectoryGame();
        string? identifier=selected.HasValue?selected.Value.GetProperty("id").GetString():null;
        // Clear stale selectable rows before any network request can fail.
        directoryRows.Clear();directoryGames.Items.Clear();
        try{ShowDirectory(await Backend.Run("directory-list",new {},Receive),identifier);directoryLoadFailed=false;UpdateDirectoryControls();}
        catch{directoryLoadFailed=true;UpdateDirectoryControls();throw;}
    }

    private async Task ConnectDirectory()
    {
        using var dialog=new Form {Text=Lang.T("directoryConnect"),ClientSize=new Size(650,260),StartPosition=FormStartPosition.CenterParent,
            FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false,MinimizeBox=false};
        var explanation=new Label {Text=Lang.T("directoryConnectInfo"),Left=18,Top=18,Width=610,Height=112};
        var code=new TextBox {Left=18,Top=142,Width=610,MaxLength=2048,UseSystemPasswordChar=true,AccessibleName=Lang.T("directoryCode")};
        var accept=new Button {Text=Lang.T("directoryConnect"),Left=410,Top=196,Width=220,Height=38,DialogResult=DialogResult.OK};
        var cancel=new Button {Text=Lang.T("cancel"),Left=250,Top=196,Width=150,Height=38,DialogResult=DialogResult.Cancel};
        dialog.Controls.AddRange([explanation,code,accept,cancel]);dialog.AcceptButton=accept;dialog.CancelButton=cancel;
        if(dialog.ShowDialog(this)!=DialogResult.OK)return;
        string secret=code.Text;code.Clear();using var channel=new InternetChannel();Task pump=channel.Run(secret,_=>{});secret="";
        JsonElement result;
        try{result=await Backend.Run("directory-connect",new {channel=channel.Name},Receive);}
        finally{channel.Dispose();try{await pump;}catch(Exception){}}
        string? selected=result.TryGetProperty("game_id",out var id)&&id.ValueKind==JsonValueKind.String?id.GetString():null;
        ShowDirectory(await Backend.Run("directory-list",new {},Receive),selected);
    }

    private async Task JoinDirectory()
    {
        var selected=SelectedDirectoryGame();
        if(!selected.HasValue||!DirectoryJoinable(selected.Value))throw new BackendError("directory_join_unavailable","");
        await JoinDirectoryGame(selected.Value.GetProperty("id").GetString()!);
    }

    private async Task JoinDirectoryGame(string id)
    {
        player.Text=directoryPlayer.Text;string name=ValidName();internetPlayer.Text=name;
        if(!ConfirmInternetConnection(publicLobby:true))return;
        publicRoomOperation=true;
        internetCreating=false;internetStage="connecting";internetOperation=true;internetInvite.Clear();internetJoin.Clear();
        SetBusy(true);tabs.SelectedTab=directoryPage;
        try
        {
            var result=await Backend.Run("directory-join",new {game_id=id,player_name=name,internet_consent=1},Receive);
            QueueDirectorySwitch(result);
            gameRunning=false;RestoreWindow();LoadSnapshot(await Backend.Run("snapshot",new {},Receive));status.Text=Lang.T("ready");
        }
        finally{internetOperation=false;publicRoomOperation=false;internetInvite.Clear();UpdateInternetControls();}
    }
}
