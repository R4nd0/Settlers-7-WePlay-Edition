using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace S7Desktop;

internal static class Program
{
    public static string AppRoot => AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
    internal static string ReleaseVersion => Assembly.GetExecutingAssembly()
        .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0]
        ?? Assembly.GetExecutingAssembly().GetName().Version?.ToString() ?? "";
    [STAThread]
    private static int Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        string key = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(AppRoot.ToLowerInvariant())))[..24];
        if(Mutex.TryOpenExisting("Local\\S7Launcher-Update-"+key,out var applyingUpdate))
        {applyingUpdate.Dispose();return 1;}
        using var single = new Mutex(true, "Local\\S7Launcher-" + key, out bool first);
        using var updateGuard = new Mutex(false, "S7Launcher-Install");
        if (!first && !args.Contains("--self-test")) return 1;
        Lang.Load(AppRoot);
        if (args.Length == 2 && args[0] == "--self-test") return MainForm.SelfTest(args[1]);
        if (args.Contains("--uninstall"))
        {
            int result = 2;
            using var form = new Form { Text = Lang.T("uninstallTitle"), ClientSize = new Size(600, 160),
                StartPosition = FormStartPosition.CenterScreen, FormBorderStyle = FormBorderStyle.FixedDialog,
                MaximizeBox = false, MinimizeBox = false, Font = new Font("Segoe UI", 10) };
            form.Controls.Add(new Label { Text = Lang.T("uninstallInfo"), Dock = DockStyle.Fill, Padding = new Padding(20) });
            form.Controls.Add(new ProgressBar { Style = ProgressBarStyle.Marquee, Dock = DockStyle.Bottom, Height = 12 });
            bool finished = false;
            form.FormClosing += (_, e) => { if (!finished) e.Cancel = true; };
            form.Shown += async (_, _) =>
            {
                try
                {
                    await Backend.Run("uninstall", new { }, _ => { });
                    Shortcuts.RemoveOwn(); result = 0;
                }
                catch (BackendError error)
                {
                    if (!args.Contains("--quiet")) MessageBox.Show(form, Lang.T(error.Code), Lang.T("errorTitle"), MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
                finally { finished = true; form.Close(); }
            };
            if (args.Contains("--quiet")) { form.ShowInTaskbar = false; form.StartPosition = FormStartPosition.Manual; form.Location = new Point(-30000,-30000); }
            Application.Run(form); return result;
        }
        Application.Run(new MainForm(args.Contains("--setup")));
        return 0;
    }
}

internal static class Lang
{
    private static Dictionary<string,string> en = Read("en"), de = Read("de");
    public static string Current { get; private set; } = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "de" ? "de" : "en";
    public static string T(string key) => (Current == "de" ? de : en).GetValueOrDefault(key, en.GetValueOrDefault(key, key));
    public static Dictionary<string,string> Read(string name)
    {
        using Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("S7Launcher.Strings." + name + ".json")!;
        return JsonSerializer.Deserialize<Dictionary<string,string>>(stream)!;
    }
    public static void Load(string root)
    {
        try
        {
            string path = Path.Combine(root,"State","language.json");
            if (File.Exists(path) && new FileInfo(path).Length < 4096)
            {
                string? value = JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(path))?.GetValueOrDefault("language");
                if (value is "de" or "en") Current = value;
            }
        }
        catch (Exception error) when (error is IOException or JsonException or UnauthorizedAccessException) { }
    }
    public static void Set(string value, bool persist)
    {
        if (value is not ("de" or "en")) throw new ArgumentException(nameof(value));
        Current = value;
        if (persist)
        {
            for(DirectoryInfo? directory=new DirectoryInfo(Program.AppRoot);directory!=null;directory=directory.Parent)
                if((directory.Attributes&FileAttributes.ReparsePoint)!=0)throw new IOException("Invalid application folder");
            string folder = Path.Combine(Program.AppRoot,"State"); Directory.CreateDirectory(folder);
            string target = Path.Combine(folder,"language.json"), temporary = target + ".tmp";
            if ((File.GetAttributes(folder) & FileAttributes.ReparsePoint) != 0) throw new IOException("Invalid state folder");
            File.WriteAllText(temporary, JsonSerializer.Serialize(new { language = value }), Encoding.UTF8);
            File.Move(temporary,target,true);
        }
    }
}

internal sealed class BackendError(string code, string details) : Exception(details)
{
    public string Code { get; } = code;
}

internal static class Shortcuts
{
    private static IEnumerable<string> Locations()
    {
        yield return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),"Settlers 7 WePlay Edition.lnk");
        yield return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu),"Programs","Settlers 7 WePlay Edition.lnk");
    }
    public static Task EnsureMissingAsync()
    {
        var done=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var worker=new Thread(()=>{try{Create(onlyMissing:true);done.SetResult();}catch(Exception error){done.SetException(error);}}) {IsBackground=true};
        worker.SetApartmentState(ApartmentState.STA);worker.Start();return done.Task;
    }
    public static void Create(bool onlyMissing=false)
    {
        string[] paths=Locations().Where(path=>!onlyMissing||!File.Exists(path)).ToArray();
        if(paths.Length==0)return;
        Type? type = Type.GetTypeFromProgID("WScript.Shell"); if (type == null) return;
        dynamic shell;
        try{shell=Activator.CreateInstance(type)!;}catch(System.Runtime.InteropServices.COMException){return;}
        foreach (string path in paths)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                dynamic link = shell.CreateShortcut(path);
                // A same-named unrelated shortcut is kept.
                if (File.Exists(path) && !string.Equals((string)link.TargetPath, Environment.ProcessPath, StringComparison.OrdinalIgnoreCase)) continue;
                link.TargetPath = Environment.ProcessPath!; link.WorkingDirectory = Program.AppRoot;
                link.IconLocation = Environment.ProcessPath!; link.Description = "Settlers 7: WePlay Edition · RC3 Internet Preview"; link.Save();
                System.Runtime.InteropServices.Marshal.FinalReleaseComObject(link);
            }
            catch(Exception error)when(error is IOException or UnauthorizedAccessException or System.Runtime.InteropServices.COMException){ }
        }
        System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell);
    }
    public static void RemoveOwn()
    {
        Type? type = Type.GetTypeFromProgID("WScript.Shell"); if (type == null) return;
        dynamic shell = Activator.CreateInstance(type)!;
        foreach (string path in Locations()) if (File.Exists(path))
        {
            dynamic link = shell.CreateShortcut(path);
            if (string.Equals((string)link.TargetPath, Environment.ProcessPath, StringComparison.OrdinalIgnoreCase)) File.Delete(path);
        }
    }
}

internal sealed record Item(string Key, string Label) { public override string ToString() => Label; }

internal sealed partial class MainForm : Form
{
    private readonly bool testing;
    private bool busy, gameRunning, loading, forceSetup, recoverState, cancelRequested,updateExit;
    private bool updatesSupported,updateChecking,remoteUpdateAvailable;
    private bool visualDetailAvailable;
    private bool midsummerToneAvailable;
    private long remoteUpdateSequence;
    private string updateMessageKey="updateAutomatic",remoteUpdateVersion="";
    private string? lastUpdateFailure;
    private readonly Label updateLastOutcome=new() { AutoSize=true,MaximumSize=new Size(880,0),ForeColor=Color.Firebrick,Visible=false };
    private readonly Label updateNotice=new() { AutoSize=true,MaximumSize=new Size(880,0) };
    private readonly Label homeUpdateNotice=new() { AutoSize=true,MaximumSize=new Size(880,0) };
    private readonly TextBox updateNotes=new() { Multiline=true,ReadOnly=true,Dock=DockStyle.Top,Height=150,ScrollBars=ScrollBars.Vertical };
    private Button updateCheck=null!,updateInstall=null!,homeUpdateInstall=null!;
    private LinkLabel globalUpdateLink=null!;
    private JsonElement snapshot;
    private readonly List<JsonElement> screens = [];
    private string lastError = "";
    private readonly Dictionary<Control,string> labels = [];
    private readonly Panel body = new() { Dock = DockStyle.Fill, Padding = new Padding(20) };
    private readonly Panel setup = new() { Dock = DockStyle.Fill };
    private readonly TabControl tabs = new() { Dock = DockStyle.Fill, Padding = new Point(16,8) };
    private readonly ComboBox language = Combo();
    private readonly Label status = new() { Dock = DockStyle.Fill, AutoEllipsis = true, TextAlign = ContentAlignment.MiddleLeft };
    private readonly Label releaseVersion = new() { AutoSize = true, Margin = new Padding(10,12,18,0), ForeColor = Color.DimGray, Text = Program.ReleaseVersion };
    private readonly ProgressBar progress = new() { Dock = DockStyle.Top, Height = 6, Visible = false };
    private readonly ComboBox source = Combo();
    private readonly TextBox destination = new() { ReadOnly = true, Dock = DockStyle.Fill };
    private readonly Label setupNotice = new() { AutoSize = true, MaximumSize = new Size(850,0) };
    private readonly Button installButton, cancelButton;
    private readonly Label homeTitle = new() { AutoSize = true, Font = new Font("Segoe UI",22,FontStyle.Bold) };
    private readonly Label homeInfo = new() { AutoSize = true, MaximumSize = new Size(850,0) };
    private readonly TextBox player = new() { Dock = DockStyle.Fill, MaxLength = 24 };
    private readonly TextBox internetPlayer = new() { Dock = DockStyle.Fill, MaxLength = 24 };
    private readonly TextBox internetJoin = new() { Dock = DockStyle.Fill, MaxLength = 2048, UseSystemPasswordChar = true };
    private readonly TextBox internetInvite = new() { Dock = DockStyle.Fill, ReadOnly = true, UseSystemPasswordChar = true };
    private readonly TextBox internetEndpoint = new() { Dock = DockStyle.Fill, MaxLength = 256 };
    private readonly ComboBox internetMode = Combo();
    private readonly Label internetPolicy = new() { AutoSize = true, MaximumSize = new Size(850,0) };
    private readonly Label internetConnection = new() { AutoSize=true,MaximumSize=new Size(850,0) };
    private readonly Label internetNext = new() { AutoSize=true,MaximumSize=new Size(850,0),BackColor=Color.FromArgb(238,245,253),Padding=new Padding(12) };
    private readonly Label internetUnavailable = new() { AutoSize=true,MaximumSize=new Size(850,0),ForeColor=Color.Firebrick };
    private readonly RadioButton internetHostChoice = new() { AutoSize=true,Appearance=Appearance.Button,FlatStyle=FlatStyle.Flat,MinimumSize=new Size(220,40),TextAlign=ContentAlignment.MiddleCenter,Padding=new Padding(10,4,10,4) };
    private readonly RadioButton internetGuestChoice = new() { AutoSize=true,Appearance=Appearance.Button,FlatStyle=FlatStyle.Flat,MinimumSize=new Size(220,40),TextAlign=ContentAlignment.MiddleCenter,Padding=new Padding(10,4,10,4) };
    private readonly CheckBox internetAdvanced = new() { AutoSize=true };
    private readonly CheckBox discordOptions = new() { AutoSize=true };
    private TableLayoutPanel internetHostPanel = null!,internetGuestPanel = null!,internetAdvancedPanel = null!,discordOptionsPanel = null!;
    private Button internetPaste = null!;
    private bool internetCreating;
    private string internetStage = "connecting";
    private Button internetCopy = null!, internetShare = null!, internetEnd = null!, internetCreate = null!, internetJoinButton = null!;
    private LinkLabel discordLink = null!;
    private readonly CheckBox discordPublish = new() { AutoSize=true };
    private readonly Label discordNotice = new() { AutoSize=true, MaximumSize=new Size(850,0) };
    private Button discordConnect = null!, discordDisconnect = null!;
    private bool discordAvailable, discordConnected;
    private readonly List<Control> internetSetupControls = [];
    private Control internetEndpointField = null!, internetInviteField = null!, internetShareRow = null!, internetEndRow = null!;
    private bool internetOperation, internetAvailable;
    private readonly Label lanNotice = new() { AutoSize = true, MaximumSize = new Size(850,0) };
    private readonly ComboBox renderer = Combo(), gpu = Combo(), monitor = Combo(), window = Combo(), resolution = Combo();
    private readonly CheckBox keepDisplay = new() { AutoSize = true }, simple = new() { AutoSize = true };
    private readonly CheckBox anisotropicFiltering = new() { AutoSize = true };
    private readonly CheckBox extendedDetail = new() { AutoSize = true };
    private readonly CheckBox detailedShadows = new() { AutoSize = true };
    private readonly CheckBox extendedShadowRange = new() { AutoSize = true };
    private readonly CheckBox midsummerTone = new() { AutoSize = true };
    private readonly CheckBox enhancedAa = new() { AutoSize = true };
    private readonly CheckBox contactShadows = new() { AutoSize = true };
    private readonly CheckBox ultraShadows = new() { AutoSize = true };
    private readonly CheckBox maximumFx = new() { AutoSize = true };
    private readonly Label activeRenderer = new() { AutoSize = true };
    private readonly Dictionary<string,ComboBox> qualities = [];
    private readonly Dictionary<string,CheckBox> effects = [];
    private readonly Dictionary<Control,string> accessibleFields = [];
    private readonly TextBox details = new() { Multiline = true, ReadOnly = true, Dock = DockStyle.Top, Height = 120, ScrollBars = ScrollBars.Vertical };
    private readonly NotifyIcon tray = new() { Icon = SystemIcons.Application, Visible = false };
    private TabPage homePage = null!, lanPage = null!, internetPage = null!, graphicsPage = null!, helpPage = null!,updatesPage=null!;
    private bool automaticMenuHeight = true;

    public MainForm(bool initialSetup = false, bool test = false)
    {
        testing = test; forceSetup = initialSetup;
        Text = Lang.T("app"); Font = new Font("Segoe UI",10); BackColor = Color.White;
        ClientSize = new Size(1020,1040); MinimumSize = new Size(880,620); StartPosition = FormStartPosition.CenterScreen;
        using(var iconStream=Assembly.GetExecutingAssembly().GetManifestResourceStream("WePlay.Icon.ico"))
            if(iconStream!=null)Icon=new Icon(iconStream);
        var header = new TableLayoutPanel { Dock = DockStyle.Top, Height = 94, Padding = new Padding(22,10,22,8), ColumnCount = 3, RowCount = 2 };
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100)); header.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize)); header.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,150));
        var title = new PictureBox { SizeMode=PictureBoxSizeMode.Zoom, Dock=DockStyle.Left, Width=360, Height=54,
            AccessibleName=Lang.T("app"), Margin=Padding.Empty };
        using(var logoStream=Assembly.GetExecutingAssembly().GetManifestResourceStream("WePlay.Logo.png"))
            if(logoStream!=null)title.Image=new Bitmap(logoStream);
        header.Controls.Add(title,0,0);
        header.Controls.Add(releaseVersion,1,0);
        language.Items.AddRange([new Item("de","Deutsch"),new Item("en","English")]); language.SelectedIndex = Lang.Current == "de" ? 0 : 1;
        header.Controls.Add(language,2,0);
        var motto=new Label { AutoSize=true,ForeColor=Color.DimGray,Font=new Font("Segoe UI",9),Margin=new Padding(0,6,0,0) };
        Bind(motto,"motto");header.Controls.Add(motto,0,1);header.SetColumnSpan(motto,3);
        var footer = new TableLayoutPanel { Dock = DockStyle.Bottom, Height = 44, Padding = new Padding(22,2,22,2),ColumnCount=3,RowCount=2 };
        footer.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));footer.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));footer.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        footer.RowStyles.Add(new RowStyle(SizeType.AutoSize));footer.RowStyles.Add(new RowStyle(SizeType.Percent,100));
        discordLink=new LinkLabel { Dock=DockStyle.Fill, AutoSize=true, TextAlign=ContentAlignment.MiddleRight,
            Padding=new Padding(16,0,0,0), AccessibleDescription=Community.DiscordUrl };
        Bind(discordLink,"discordOpen");discordLink.LinkClicked+=(_,_)=>
        {try{Community.OpenDiscord();}catch(BackendError error){status.Text=Lang.T(error.Code);}};
        globalUpdateLink=new LinkLabel { AutoSize=true,Dock=DockStyle.Fill,TextAlign=ContentAlignment.MiddleRight,Padding=new Padding(12,0,0,0),Visible=false };
        globalUpdateLink.LinkClicked+=async (_,_)=>await Guard(InstallUpdate);
        footer.Controls.Add(progress,0,0);footer.SetColumnSpan(progress,3);
        footer.Controls.Add(status,0,1);footer.Controls.Add(globalUpdateLink,1,1);footer.Controls.Add(discordLink,2,1);
        Controls.Add(body); Controls.Add(footer); Controls.Add(header);
        body.Controls.Add(tabs); body.Controls.Add(setup);
        installButton=Button("install",InstallGame,true);
        cancelButton=new Button {AutoSize=true,MinimumSize=new Size(150,38)};Bind(cancelButton,"cancel");cancelButton.Visible=false;
        cancelButton.Click+=(_,_)=>RequestCancellation();
        BuildSetup(); BuildTabs();
        tabs.SelectedIndexChanged += (_,_) => { if(IsHandleCreated) BeginInvoke((Action)FitMenuHeight); };
        ResizeBegin += (_,_) => automaticMenuHeight=false;
        tabs.Visible=false;
        language.SelectedIndexChanged+=(_,_)=>ChangeLanguage();
        tray.DoubleClick += (_,_) => RestoreWindow();
        var trayMenu = new ContextMenuStrip(); trayMenu.Items.Add(Lang.T("show"),null,(_,_)=>RestoreWindow()); trayMenu.Items.Add(Lang.T("exit"),null,(_,_)=>Close()); tray.ContextMenuStrip = trayMenu;
        Resize += (_,_) => { if (gameRunning && WindowState == FormWindowState.Minimized) { ShowInTaskbar = false; tray.Visible = true; } };
        FormClosing += async (_,e) =>
        {
            if(updateExit)return;
            if (gameRunning) { e.Cancel = true; WindowState = FormWindowState.Minimized; ShowInTaskbar = false; tray.Visible = true; }
            else if (busy) { e.Cancel = true; if (MessageBox.Show(this,Lang.T("closeBusy"),Lang.T("app"),MessageBoxButtons.YesNo,MessageBoxIcon.Question)==DialogResult.Yes) RequestCancellation(); }
            else if(GraphicsDirty&&!closeDraftApproved)
            {
                e.Cancel=true;if(closingDraft)return;closingDraft=true;bool accepted=false;
                await Guard(async()=>accepted=await ResolveGraphicsDraft());closingDraft=false;
                if(accepted&&!IsDisposed){closeDraftApproved=true;Close();}
            }
        };
        FormClosed += (_,_) => { tray.Dispose(); };
        Shown += async (_,_) => { if (!testing) await Initialise(); };
        Load += (_,_) =>
        {
            Rectangle area=Screen.FromControl(this).WorkingArea;
            MinimumSize=new Size(Math.Min(MinimumSize.Width,area.Width-16),Math.Min(MinimumSize.Height,area.Height-16));
            Size=new Size(Math.Min(Width,area.Width-16),Math.Min(Height,area.Height-16));
            BeginInvoke((Action)FitMenuHeight);
        };
        ApplyLanguage();
    }

    private void FitMenuHeight()
    {
        if(!automaticMenuHeight||!Visible||WindowState!=FormWindowState.Normal)return;
        PerformLayout();
        Panel? scroll=setup.Visible?setup:tabs.SelectedTab?.Controls.OfType<Panel>().FirstOrDefault(panel=>panel.AutoScroll);
        if(scroll==null)return;
        scroll.PerformLayout();
        int extra=scroll.DisplayRectangle.Height-scroll.ClientSize.Height;
        if(extra<=0)return;
        Rectangle area=Screen.FromControl(this).WorkingArea;
        int target=Math.Min(Height+extra+8,area.Height-16);
        if(target<=Height)return;
        Height=target;
        Top=Math.Clamp(Top,area.Top+8,Math.Max(area.Top+8,area.Bottom-Height-8));
        scroll.AutoScrollPosition=Point.Empty;
    }

    private static ComboBox Combo() => new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill, IntegralHeight = false, DropDownHeight = 280 };
    private void Bind(Control control,string key) { labels[control] = key; control.Text = Lang.T(key); }
    private Label Label(string key) { var result = new Label { AutoSize = true, Margin = new Padding(0,10,0,6), MaximumSize = new Size(900,0) }; Bind(result,key); return result; }
    private Button Button(string key,Func<Task> action,bool primary = false,bool exclusive=true)
    {
        var result = new Button { AutoSize = true, MinimumSize = new Size(150,38), Padding = new Padding(10,3,10,3), Margin = new Padding(0,6,10,6), FlatStyle = FlatStyle.Flat };
        result.FlatAppearance.BorderColor = Color.FromArgb(190,200,213);
        if (primary) { result.BackColor = Color.FromArgb(30,91,164); result.ForeColor = Color.White; }
        Bind(result,key);result.Click+=async(_,_)=>{if(exclusive)await Guard(action);else await action();};return result;
    }
    private static FlowLayoutPanel Buttons(params Control[] controls)
    {
        var row = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Top, WrapContents = true, Margin = new Padding(0,8,0,8) };
        row.Controls.AddRange(controls); return row;
    }
    private static TableLayoutPanel Stack(Control host)
    {
        var result = new TableLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Top, ColumnCount = 1, Padding = new Padding(14) };
        result.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100)); host.Controls.Add(result); return result;
    }
    private static void Add(TableLayoutPanel stack,Control control)
    {
        control.Anchor = AnchorStyles.Left | AnchorStyles.Right; stack.RowStyles.Add(new RowStyle(SizeType.AutoSize)); stack.Controls.Add(control,0,stack.RowCount++);
    }
    private Control Field(string key,Control value,Control? extra = null)
    {
        accessibleFields[value]=key;value.AccessibleName=Lang.T(key);
        var outer = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 2, Margin = new Padding(0,2,0,8) };
        accessibleFields[outer]=key;outer.AccessibleName=Lang.T(key);
        outer.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100)); outer.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        var label = Label(key); outer.Controls.Add(label,0,0); outer.SetColumnSpan(label,2);
        value.Margin = new Padding(0,0,10,0); outer.Controls.Add(value,0,1);
        if (extra != null) outer.Controls.Add(extra,1,1);
        return outer;
    }
    private void BuildSetup()
    {
        var scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true }; setup.Controls.Add(scroll); var stack = Stack(scroll);
        var title = Label("setupTitle"); title.Font = new Font(Font.FontFamily,20,FontStyle.Bold); Add(stack,title); Add(stack,Label("setupIntro"));
        Add(stack,Field("source",source,Button("browseSource",BrowseSource)));
        Add(stack,Field("destination",destination,Button("change",BrowseDestination)));
        Add(stack,Label("storagePolicy"));
        Add(stack,Label("installInfo")); Add(stack,setupNotice); Add(stack,Buttons(installButton,cancelButton));
    }
    private void BuildTabs()
    {
        homePage = new TabPage(); Bind(homePage,"play"); homePage.BackColor = Color.White;
        lanPage = new TabPage(); Bind(lanPage,"lan"); lanPage.BackColor = Color.White;
        internetPage = new TabPage(); Bind(internetPage,"internet"); internetPage.BackColor = Color.White;
        graphicsPage = new TabPage(); Bind(graphicsPage,"graphics"); graphicsPage.BackColor = Color.White;
        helpPage = new TabPage(); Bind(helpPage,"help"); helpPage.BackColor = Color.White;
        updatesPage=new TabPage();Bind(updatesPage,"updates");updatesPage.BackColor=Color.White;
        directoryPage=BuildDirectory();
        tabs.TabPages.AddRange([homePage,lanPage,internetPage,directoryPage,graphicsPage,helpPage,updatesPage]);
        var homeScroll=new Panel { Dock=DockStyle.Fill,AutoScroll=true };homePage.Controls.Add(homeScroll);
        var home = Stack(homeScroll); Add(home,homeTitle); Add(home,homeInfo);
        Add(home,maintenanceNotice);
        maintenanceNotice.LinkClicked+=(_,_)=>{tabs.SelectedTab=helpPage;maintenanceCleanup.Focus();helpPage.Controls.OfType<Panel>().FirstOrDefault()?.ScrollControlIntoView(maintenanceCleanup);};
        Add(home,BuildPlayModes());Add(home,Label("modeConsentInfo"));
        homeScroll.ClientSizeChanged+=(_,_)=>ResizePlayModes(homeScroll);
        ResizePlayModes(homeScroll);
        Add(home,Buttons(Button("openGraphics",()=>{tabs.SelectedTab=graphicsPage;return Task.CompletedTask;})));
        Add(home,homeUpdateNotice);
        homeUpdateInstall=Button("updateInstall",InstallUpdate,true);
        Add(home,Buttons(Button("updates",()=>{tabs.SelectedTab=updatesPage;return Task.CompletedTask;}),homeUpdateInstall));
        var lanScroll=new Panel { Dock=DockStyle.Fill,AutoScroll=true };lanPage.Controls.Add(lanScroll);var lan = Stack(lanScroll);
        var lanTitle=Label("lanTitle");lanTitle.Font=new Font(Font.FontFamily,20,FontStyle.Bold);Add(lan,lanTitle);
        Add(lan,Label("lanIntro"));Add(lan,Field("player",player));
        lanActionBar=Buttons(Button("lanStart",()=>Play(true),true));DockActions(lanPage,lanActionBar);
        Add(lan,Label("lanSteps"));Add(lan,Label("lanTrouble"));
        Add(lan,Buttons(Button("lanPermission",Firewall)));Add(lan,lanNotice);
        var internetScroll = new Panel { Dock=DockStyle.Fill, AutoScroll=true }; internetPage.Controls.Add(internetScroll);
        var net = Stack(internetScroll);
        var netTitle = Label("internetTitle"); netTitle.Font=new Font(Font.FontFamily,20,FontStyle.Bold); Add(net,netTitle);
        Add(net,Label("internetIntro"));Bind(internetUnavailable,"internetUnavailable");Add(net,internetUnavailable);
        Add(net,Label("internetNetworkSummary"));
        Add(net,internetConnection);Add(net,internetNext);
        void AddSetup(Control control){internetSetupControls.Add(control);Add(net,control);}
        AddSetup(Field("player",internetPlayer));
        Bind(internetHostChoice,"internetHostChoice");Bind(internetGuestChoice,"internetGuestChoice");internetHostChoice.Checked=true;
        AddSetup(Buttons(internetHostChoice,internetGuestChoice));
        internetHostPanel=new TableLayoutPanel { AutoSize=true,Dock=DockStyle.Top,ColumnCount=1 };internetHostPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        accessibleFields[internetHostPanel]="internetHostChoice";
        Add(internetHostPanel,Label("internetHostSteps"));
        internetCreate=Button("internetCreate",()=>PlayInternet(true),true);
        Bind(internetAdvanced,"internetAdvanced");Add(internetHostPanel,internetAdvanced);
        internetAdvancedPanel=new TableLayoutPanel { AutoSize=true,Dock=DockStyle.Top,ColumnCount=1 };internetAdvancedPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        accessibleFields[internetAdvancedPanel]="internetAdvanced";
        Add(internetAdvancedPanel,Field("internetMode",internetMode));Add(internetAdvancedPanel,internetPolicy);
        internetEndpointField=Field("internetEndpoint",internetEndpoint);Add(internetAdvancedPanel,internetEndpointField);
        Add(internetHostPanel,internetAdvancedPanel);AddSetup(internetHostPanel);
        internetGuestPanel=new TableLayoutPanel { AutoSize=true,Dock=DockStyle.Top,ColumnCount=1 };internetGuestPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        accessibleFields[internetGuestPanel]="internetGuestChoice";
        Add(internetGuestPanel,Label("internetGuestSteps"));
        internetPaste=Button("internetPaste",()=>{PasteInternetInvitation();return Task.CompletedTask;});
        Add(internetGuestPanel,Field("internetJoinCode",internetJoin,internetPaste));
        internetJoinButton=Button("internetJoin",()=>PlayInternet(false),true);AddSetup(internetGuestPanel);
        internetCopy = new Button { AutoSize=true, MinimumSize=new Size(150,38), Padding=new Padding(10,3,10,3) };
        Bind(internetCopy,"internetCopy"); internetCopy.Click += (_,_) =>
        {
            if(internetOperation && internetInvite.Text.Length>0)
                try{Clipboard.SetText(internetInvite.Text);}catch(System.Runtime.InteropServices.ExternalException){status.Text=Lang.T("internetClipboardFailed");}
        };
        internetShare=new Button { AutoSize=true,MinimumSize=new Size(150,38),Padding=new Padding(10,3,10,3) };
        Bind(internetShare,"discordRoomShare");internetShare.Click+=(_,_)=>
        {
            if(internetOperation&&internetInvite.Text.Length>0)
                try{Clipboard.SetText(Community.InvitationMessage(internetInvite.Text,Lang.Current));status.Text=Lang.T("discordRoomCopied");}
                catch(System.Runtime.InteropServices.ExternalException){status.Text=Lang.T("internetClipboardFailed");}
        };
        internetInviteField=Field("internetOwnCode",internetInvite);Add(net,internetInviteField);
        internetShareRow=Buttons(internetCopy,internetShare);Add(net,internetShareRow);
        internetEnd=new Button { AutoSize=true,MinimumSize=new Size(150,38),Padding=new Padding(10,3,10,3) };
        Bind(internetEnd,"internetEnd"); internetEnd.Click += (_,_) =>
        {
            if(internetOperation&&!gameRunning)RequestCancellation();
        };
        internetEndRow=Buttons(internetCreate,internetJoinButton,internetEnd);DockActions(internetPage,internetEndRow);Add(net,Label("internetSafety"));
        internetHostChoice.CheckedChanged+=(_,_)=>UpdateInternetControls();internetGuestChoice.CheckedChanged+=(_,_)=>UpdateInternetControls();
        internetAdvanced.CheckedChanged+=(_,_)=>{UpdateInternetControls();FitMenuHeight();if(internetAdvanced.Checked)internetScroll.ScrollControlIntoView(internetAdvancedPanel);};
        internetJoin.TextChanged+=(_,_)=>UpdateInternetControls();discordPublish.CheckedChanged+=(_,_)=>UpdateInternetControls();
        internetMode.SelectedIndexChanged += (_,_)=>UpdateInternetControls();
        BuildGraphicsPage();
        var helpScroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true }; helpPage.Controls.Add(helpScroll); var h = Stack(helpScroll);
        Add(h,Label("helpIntro"));
        var ways=Label("startGuideTitle");ways.Font=new Font(Font.FontFamily,12,FontStyle.Bold);Add(h,ways);
        Add(h,Label("startGuide"));Add(h,Label("roomGuide"));Add(h,Label("invitationGuide"));
        var maintenance=Label("maintenanceTitle");maintenance.Font=new Font(Font.FontFamily,12,FontStyle.Bold);Add(h,maintenance);
        Add(h,Label("maintenanceGuide"));
        Add(h,Buttons(Button("verify",Verify),Button("repair",ShowRepair)));
        Add(h,Buttons(Button("fallback",Fallback),Button("probe",Probe),Button("lanPermission",Firewall)));
        maintenanceCleanup=Button("cleanup",Cleanup);
        Add(h,Buttons(maintenanceCleanup,Button("diagnostics",Diagnostics))); Add(h,Label("details")); Add(h,details);
        Add(h,Label("discordTitle"));Add(h,Label("discordInfo"));
        var discordCode=new TextBox { Text=Community.DiscordCode,ReadOnly=true,Dock=DockStyle.Fill };
        Add(h,Field("discordSuggestedCode",discordCode));
        Add(h,Buttons(Button("discordOpen",()=>{Community.OpenDiscord();return Task.CompletedTask;}),
            Button("discordCopyLink",()=>{try{Clipboard.SetText(Community.DiscordUrl);status.Text=Lang.T("discordLinkCopied");}
                catch(System.Runtime.InteropServices.ExternalException){throw new BackendError("internetClipboardFailed","");}return Task.CompletedTask;})));
        var updateScroll=new Panel { Dock=DockStyle.Fill,AutoScroll=true };updatesPage.Controls.Add(updateScroll);var u=Stack(updateScroll);
        var updateTitle=Label("updatesTitle");updateTitle.Font=new Font(Font.FontFamily,20,FontStyle.Bold);Add(u,updateTitle);
        Add(u,Label("updatesIntro"));Add(u,updateNotice);
        Add(u,updateLastOutcome);
        updateCheck=Button("updateCheck",()=>CheckUpdates(false));updateInstall=Button("updateInstall",InstallUpdate,true);
        Add(u,Buttons(updateCheck,updateInstall));Add(u,Label("updateNotes"));Add(u,updateNotes);
        Add(u,Label("updatesPreserve"));
        accessibleFields[updateNotes]="updateNotes";
        TrackGraphicsDraft();
    }

    private void ApplyLanguage()
    {
        foreach (var item in labels) item.Key.Text = Lang.T(item.Value);
        ApplyPlayModeLanguage();UpdateMaintenanceNotice();
        foreach (var item in accessibleFields) item.Key.AccessibleName = Lang.T(item.Value);
        Text=Lang.T("app"); tray.Text=Lang.T("app");
        loading=true;
        int oldRenderer=renderer.SelectedIndex,oldWindow=window.SelectedIndex;
        renderer.Items.Clear();renderer.Items.AddRange([Lang.T("auto"),Lang.T("modern"),Lang.T("native")]); renderer.SelectedIndex=Math.Max(0,oldRenderer);
        window.Items.Clear();window.Items.AddRange([Lang.T("windowed"),Lang.T("borderless")]); window.SelectedIndex=Math.Max(0,oldWindow);
        int oldInternet=internetMode.SelectedIndex;internetMode.Items.Clear();internetMode.Items.AddRange([Lang.T("internetAssisted"),Lang.T("internetDirect")]);internetMode.SelectedIndex=Math.Max(0,oldInternet);
        foreach(var combo in qualities.Values) { int old=combo.SelectedIndex;combo.Items.Clear();combo.Items.AddRange([Lang.T("low"),Lang.T("medium"),Lang.T("high")]);combo.SelectedIndex=old<0?1:old; }
        loading=false;
        if(snapshot.ValueKind==JsonValueKind.Object) LoadSnapshot(snapshot,false);
        homeTitle.Text=Lang.T(gameRunning?"running":"playTitle"); homeInfo.Text=Lang.T(gameRunning?"runningInfo":"playIntro");
        if(!busy) status.Text=Lang.T("ready");
        UpdateInternetControls();
        RefreshUpdateControls();
    }
    private async Task Initialise()
    {
        try
        {
            SetBusy(true);
            var value=await Backend.Run("snapshot",new {},Receive); LoadSnapshot(value);
            bool installed=value.GetProperty("installed").GetBoolean();
            if(!installed||value.GetProperty("update_available").GetBoolean()||
                (value.TryGetProperty("needs_repair",out var repair)&&repair.GetBoolean())) await ShowSetup();
            else
            {
                ShowMain();
                _=EnsureMissingShortcuts();
            }
        }
        catch(Exception error)
        {
            ShowError(error);setup.Visible=true;tabs.Visible=false;installButton.Enabled=true;
            if(error is BackendError portableFailure&&portableFailure.Code=="standalone_invalid")setup.Visible=false;
            if(error is BackendError failure&&failure.Code=="installation_invalid"){recoverState=true;Bind(installButton,"restartSetup");}
        }
        finally {SetBusy(false);}
        if(!testing&&updatesSupported)_=CheckUpdates(true);
    }
    private void LoadSnapshot(JsonElement value,bool replaceFields=true,bool acceptGraphics=false)
    {
        if(GraphicsDirty&&!acceptGraphics)replaceFields=false;
        snapshot=value.Clone();loading=true;
        maintenanceCount=value.TryGetProperty("maintenance_pending",out var pendingMaintenance)?pendingMaintenance.GetInt32():0;UpdateMaintenanceNotice();
        visualDetailAvailable=value.TryGetProperty("visual_detail_supported",out var visualAvailable)&&visualAvailable.GetBoolean();
        midsummerToneAvailable=value.TryGetProperty("midsummer_tone_supported",out var toneAvailable)&&toneAvailable.GetBoolean();
        updatesSupported=value.TryGetProperty("updates_supported",out var updatesAvailable)&&updatesAvailable.GetBoolean();
        lastUpdateFailure=value.TryGetProperty("update_last_error",out var updateFailure)&&updateFailure.ValueKind==JsonValueKind.String?updateFailure.GetString():null;
        screens.Clear();screens.AddRange(value.GetProperty("displays").EnumerateArray().Select(x=>x.Clone()));
        if(replaceFields)
        {
            destination.Text=value.GetProperty("data_default").GetString(); player.Text=value.GetProperty("player_name").GetString();internetPlayer.Text=player.Text;
            string policy=value.GetProperty("renderer_policy").GetString()!;renderer.SelectedIndex=policy=="dxvk"?1:policy=="native"?2:0;
            simple.Checked=value.GetProperty("choice").TryGetProperty("simple_menu",out var simpleValue)&&simpleValue.GetBoolean();
            anisotropicFiltering.Checked=value.GetProperty("choice").TryGetProperty("anisotropic_filtering",out var filteringValue)&&filteringValue.GetBoolean();
            extendedDetail.Checked=value.GetProperty("choice").TryGetProperty("extended_detail",out var detailValue)&&detailValue.GetBoolean();
            detailedShadows.Checked=value.GetProperty("choice").TryGetProperty("detailed_shadows",out var shadowValue)&&shadowValue.GetBoolean();
            extendedShadowRange.Checked=value.GetProperty("choice").TryGetProperty("extended_shadow_range",out var rangeValue)&&rangeValue.GetBoolean();
            midsummerTone.Checked=midsummerToneAvailable&&value.GetProperty("choice").TryGetProperty("midsummer_tone",out var toneValue)&&toneValue.GetBoolean();
            enhancedAa.Checked=midsummerToneAvailable&&value.GetProperty("choice").TryGetProperty("enhanced_aa",out var aaValue)&&aaValue.GetBoolean();
            contactShadows.Checked=midsummerToneAvailable&&value.GetProperty("choice").TryGetProperty("contact_shadows",out var contactsValue)&&contactsValue.GetBoolean();
            ultraShadows.Checked=visualDetailAvailable&&value.GetProperty("choice").TryGetProperty("ultra_shadows",out var ultraValue)&&ultraValue.GetBoolean();
            maximumFx.Checked=midsummerToneAvailable&&value.GetProperty("choice").TryGetProperty("maximum_fx",out var maximumValue)&&maximumValue.GetBoolean();
            var quality=value.GetProperty("quality");foreach(var pair in qualities)pair.Value.SelectedIndex=quality.GetProperty(pair.Key).GetInt32();
            foreach(var pair in effects)pair.Value.Checked=quality.GetProperty(pair.Key).GetInt32()!=0;
        }
        internetAvailable=value.TryGetProperty("internet_available",out var available)&&available.GetBoolean();
        discordAvailable=value.TryGetProperty("discord_available",out var discordCapability)&&discordCapability.GetBoolean();
        discordConnected=value.TryGetProperty("discord_connected",out var connected)&&connected.GetBoolean();
        directoryAvailable=value.TryGetProperty("directory_available",out var directoryCapability)&&directoryCapability.GetBoolean();
        publicHostingAvailable=value.TryGetProperty("public_directory_available",out var publicCapability)&&publicCapability.GetBoolean();
        directoryConnected=value.TryGetProperty("directory_connected",out var directoryConnection)&&directoryConnection.GetBoolean();
        if(replaceFields)directoryPlayer.Text=player.Text;
        UpdateDirectoryControls();
        if(replaceFields)discordPublish.Checked=value.TryGetProperty("discord_publish_enabled",out var publication)&&publication.GetBoolean();
        activeRenderer.Text=Lang.T("activeRenderer")+": "+Lang.T(value.GetProperty("renderer").GetString()=="dxvk"?"modern":"native");
        string? oldGpu=replaceFields?value.GetProperty("choice").GetProperty("choice").GetString():(gpu.SelectedItem as Item)?.Key;
        gpu.Items.Clear();gpu.Items.Add(new Item("",Lang.T("gpuAuto")));
        foreach(var item in value.GetProperty("probe").GetProperty("devices").EnumerateArray())gpu.Items.Add(new Item(item.GetString()!,item.GetString()!));
        gpu.SelectedIndex=0;for(int i=1;i<gpu.Items.Count;i++)if(gpu.Items[i] is Item option&&option.Key==oldGpu)gpu.SelectedIndex=i;
        var selected=value.GetProperty("choice").TryGetProperty("display",out var found)?found:default;
        string? oldDisplay=replaceFields&&selected.ValueKind==JsonValueKind.Object?selected.GetProperty("device").GetString():(monitor.SelectedItem as Item)?.Key;
        monitor.Items.Clear();foreach(var item in screens)
        {
            string key=item.GetProperty("device").GetString()!;
            monitor.Items.Add(new Item(key,key.Split("DISPLAY").Last()+ (item.GetProperty("primary").GetBoolean()?" ("+Lang.T("primary")+")":"")+" · "+item.GetProperty("width")+" × "+item.GetProperty("height")));
        }
        if(monitor.Items.Count>0){monitor.SelectedIndex=0;for(int i=0;i<monitor.Items.Count;i++)if(monitor.Items[i] is Item option&&option.Key==oldDisplay)monitor.SelectedIndex=i;}
        if(replaceFields){keepDisplay.Checked=selected.ValueKind!=JsonValueKind.Object;window.SelectedIndex=selected.ValueKind==JsonValueKind.Object&&selected.GetProperty("mode").GetString()=="borderless"?1:0;}
        UpdateResolutions(allowWhileLoading:true);
        if(replaceFields&&selected.ValueKind==JsonValueKind.Object&&window.SelectedIndex==0)
        {
            string key=selected.GetProperty("width")+"x"+selected.GetProperty("height");
            for(int i=0;i<resolution.Items.Count;i++)if(resolution.Items[i] is Item option&&option.Key==key)resolution.SelectedIndex=i;
        }
        loading=false;
        if(replaceFields||acceptGraphics)AcceptGraphicsDraft();else UpdateGraphicsDraft();
    }
    private void ShowMain(){setup.Visible=false;tabs.Visible=true;forceSetup=false;homeTitle.Text=Lang.T("playTitle");homeInfo.Text=Lang.T("playIntro");status.Text=Lang.T("ready");FitMenuHeight();}
    private async Task ShowSetup()
    {
        if(!await ResolveGraphicsDraft())return;
        tabs.Visible=false;setup.Visible=true;
        Bind(installButton,snapshot.GetProperty("installed").GetBoolean()?"update":"install");
        source.Items.Clear();
        Bind(setupNotice,"detecting");var found=await Backend.Run("detect",new {},Receive);
        foreach(var item in found.GetProperty("sources").EnumerateArray())
        {
            string path=item.GetProperty("path").GetString()!;
            if(!source.Items.Cast<Item>().Any(x=>x.Key==path))source.Items.Add(new Item(path,path));
        }
        if(source.Items.Count>0)
        {
            source.SelectedIndex=0;
            bool own=snapshot.GetProperty("installed").GetBoolean()&&
                (source.SelectedItem as Item)?.Key==snapshot.GetProperty("current").GetString();
            string message=own?"managedFound":"found";
            Bind(setupNotice,message);status.Text=Lang.T(message);
        }
        else {Bind(setupNotice,"notFound");status.Text=Lang.T("notFound");}
        FitMenuHeight();
    }
    private Task BrowseSource()
    {
        using var dialog=new FolderBrowserDialog { Description=Lang.T("browseHint"), UseDescriptionForTitle=true, ShowNewFolderButton=false };
        if(dialog.ShowDialog(this)==DialogResult.OK){source.Items.Add(new Item(dialog.SelectedPath,dialog.SelectedPath));source.SelectedIndex=source.Items.Count-1;}
        return Task.CompletedTask;
    }
    private Task BrowseDestination()
    {
        using var dialog=new FolderBrowserDialog { Description=Lang.T("dataHint"),UseDescriptionForTitle=true };
        if(dialog.ShowDialog(this)==DialogResult.OK)destination.Text=Path.Combine(dialog.SelectedPath,"Settlers7WePlay-Games");
        return Task.CompletedTask;
    }
    private async Task InstallGame()
    {
        if(recoverState)
        {
            if(MessageBox.Show(this,Lang.T("resetConfirm"),Lang.T("app"),MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
            LoadSnapshot(await Backend.Run("reset-installation",new {},Receive));recoverState=false;await ShowSetup();return;
        }
        if(source.SelectedItem is not Item item)throw new BackendError("source_missing","");
        var result=await Backend.Run("install",new {source=item.Key,container=destination.Text},Receive);
        LoadSnapshot(result);if(!testing)Shortcuts.Create();ShowMain();
    }
    private void RefreshUpdateControls()
    {
        if(updateCheck==null)return;
        updateCheck.Enabled=updatesSupported&&!busy&&!updateChecking;
        updateInstall.Enabled=homeUpdateInstall.Enabled=updatesSupported&&remoteUpdateAvailable&&!busy&&!gameRunning&&!updateChecking;
        updateInstall.Visible=homeUpdateInstall.Visible=remoteUpdateAvailable;
        globalUpdateLink.Visible=remoteUpdateAvailable;
        globalUpdateLink.Enabled=updateInstall.Enabled;
        globalUpdateLink.Text=string.Format(Lang.T("updateGlobal"),remoteUpdateVersion);
        updateLastOutcome.Visible=lastUpdateFailure!=null;
        updateLastOutcome.Text=lastUpdateFailure==null?"":string.Format(Lang.T("updateLastFailure"),Lang.T(lastUpdateFailure));
        string key=!updatesSupported?"update_unavailable":updateChecking?"updateChecking":updateMessageKey;
        updateNotice.Text=homeUpdateNotice.Text=string.Format(Lang.T(key),remoteUpdateVersion)+(updateCheckIncomplete?" "+Lang.T("updateIncomplete"):"");
        homeUpdateNotice.ForeColor=remoteUpdateAvailable?Color.FromArgb(30,91,164):Color.DimGray;
        FontStyle style=remoteUpdateAvailable?FontStyle.Bold:FontStyle.Regular;
        if(homeUpdateNotice.Font.Style!=style)homeUpdateNotice.Font=remoteUpdateAvailable?new Font(Font,style):Font;
    }
    private void ShowUpdateResult(JsonElement value)
    {
        remoteUpdateAvailable=value.GetProperty("available").GetBoolean();
        remoteUpdateSequence=remoteUpdateAvailable?value.GetProperty("sequence").GetInt64():0;
        remoteUpdateVersion=remoteUpdateAvailable?value.GetProperty("version").GetString()??"":"";
        updateMessageKey=remoteUpdateAvailable?"updateFound":value.GetProperty("reason").GetString()=="no_signed_release"?"updateNoRelease":"updateCurrent";
        updateNotes.Text=value.GetProperty("notes").GetString()??"";
        updateCheckIncomplete=value.TryGetProperty("incomplete",out var incomplete)&&incomplete.GetBoolean();RefreshUpdateControls();
    }
    private async Task CheckUpdates(bool automatic)
    {
        if(!updatesSupported||updateChecking)return;
        updateChecking=true;updateNotice.Text=homeUpdateNotice.Text=Lang.T("updateChecking");RefreshUpdateControls();
        try
        {
            var value=await Backend.Run("update-check",new {},_=>{},trackOperation:false,cancellationToken:backgroundLifetime.Token);
            if(IsDisposed)return;
            ShowUpdateResult(value);
        }
        catch(Exception error)
        {
            if(IsDisposed)return;
            updateMessageKey=error is BackendError backend?backend.Code:"update_download_failed";RefreshUpdateControls();
            if(!automatic)throw;
        }
        finally {updateChecking=false;if(!IsDisposed)RefreshUpdateControls();}
    }
    private async Task InstallUpdate()
    {
        if(gameRunning||!remoteUpdateAvailable)throw new BackendError("update_busy","");
        if(!await ResolveGraphicsDraft())return;
        await Backend.Run("update-install",new {sequence=remoteUpdateSequence},Receive);
        updateExit=true;Close();
    }
    private void UpdateResolutions(bool allowWhileLoading=false)
    {
        if(loading&&!allowWhileLoading)return;
        string? old=(resolution.SelectedItem as Item)?.Key;resolution.Items.Clear();
        if(monitor.SelectedIndex>=0&&monitor.SelectedIndex<screens.Count)
        {
            var screen=screens[monitor.SelectedIndex];int width=screen.GetProperty("width").GetInt32(),height=screen.GetProperty("height").GetInt32();
            foreach(var size in screen.GetProperty("resolutions").EnumerateArray())
            {
                int w=size[0].GetInt32(),h=size[1].GetInt32();
                if(window.SelectedIndex!=1||(w==width&&h==height))resolution.Items.Add(new Item(w+"x"+h,w+" × "+h));
            }
            if(resolution.Items.Count>0){resolution.SelectedIndex=resolution.Items.Count-1;for(int i=0;i<resolution.Items.Count;i++)if(resolution.Items[i] is Item option&&option.Key==old)resolution.SelectedIndex=i;}
            if(window.SelectedIndex==1&&(width>3840||height>3840||(long)width*height>3840L*2160||resolution.Items.Count==0))
            {status.Text=Lang.T("unsupportedSize");window.SelectedIndex=0;return;}
        }
        DisplayEnabled();
    }
    private void DisplayEnabled()
    {
        monitor.Enabled=window.Enabled=!keepDisplay.Checked&&!busy;resolution.Enabled=!keepDisplay.Checked&&!busy&&window.SelectedIndex==0;
        gpu.Enabled=!busy&&renderer.SelectedIndex!=2&&gpu.Items.Count>1;
        anisotropicFiltering.Enabled=!busy&&renderer.SelectedIndex!=2;
        extendedDetail.Enabled=detailedShadows.Enabled=extendedShadowRange.Enabled=!busy&&visualDetailAvailable;
        ultraShadows.Enabled=!busy&&visualDetailAvailable;
        midsummerTone.Enabled=enhancedAa.Enabled=contactShadows.Enabled=maximumFx.Enabled=!busy&&midsummerToneAvailable&&renderer.SelectedIndex!=2;
        if(renderer.SelectedIndex==2&&!loading)midsummerTone.Checked=enhancedAa.Checked=contactShadows.Checked=maximumFx.Checked=false;
    }
    private void ApplyQualityPreset(bool maximumObjects)
    {
        foreach(var pair in qualities)pair.Value.SelectedIndex=pair.Key=="ObjectQuality"&&!maximumObjects?1:2;
        foreach(var pair in effects)pair.Value.Checked=pair.Key!="DepthOfField";
        anisotropicFiltering.Checked=true;
    }
    private void ApplyRecommendedGraphics()
    {
        renderer.SelectedIndex=1;
        DisplayEnabled();
        ApplyQualityPreset(true);
        extendedDetail.Checked=detailedShadows.Checked=extendedShadowRange.Checked=visualDetailAvailable;
        ultraShadows.Checked=false;
        maximumFx.Checked=false;
        midsummerTone.Checked=contactShadows.Checked=false;
        enhancedAa.Checked=midsummerToneAvailable;
    }
    private object GraphicsRequest(bool compatible=false)
    {
        object? output=null;
        if(!keepDisplay.Checked)
        {
            if(monitor.SelectedItem is not Item item||resolution.SelectedItem is not Item size)throw new BackendError("request_invalid","");
            string[] dimensions=size.Key.Split('x');output=new {device=item.Key,width=int.Parse(dimensions[0]),height=int.Parse(dimensions[1]),mode=window.SelectedIndex==1?"borderless":"windowed"};
        }
        var quality=qualities.ToDictionary(x=>x.Key,x=>x.Value.SelectedIndex);foreach(var pair in effects)quality[pair.Key]=pair.Value.Checked?1:0;
        string policy=compatible?"native":renderer.SelectedIndex==1?"dxvk":renderer.SelectedIndex==2?"native":"auto";
        string? card=compatible||policy=="native"?null:(gpu.SelectedItem as Item)?.Key;
        if(string.IsNullOrEmpty(card))card=null;
        return new {renderer_policy=policy,choice=new {format="s7-graphics-choice-v1",choice=card,simple_menu=simple.Checked,display=output,
            anisotropic_filtering=anisotropicFiltering.Checked,extended_detail=extendedDetail.Checked,
            detailed_shadows=detailedShadows.Checked,extended_shadow_range=extendedShadowRange.Checked,midsummer_tone=midsummerTone.Checked,
            enhanced_aa=enhancedAa.Checked,contact_shadows=contactShadows.Checked,ultra_shadows=ultraShadows.Checked,maximum_fx=maximumFx.Checked},quality,player_name=ValidName()};
    }
    private string ValidName()
    {
        string name=player.Text;
        if(name.Trim()!=name||Encoding.UTF8.GetByteCount(name) is <1 or >24||name.EnumerateRunes().Any(r=>
            r.Value is 0x2028 or 0x2029||Rune.GetUnicodeCategory(r) is UnicodeCategory.Control or UnicodeCategory.Format or UnicodeCategory.PrivateUse or UnicodeCategory.OtherNotAssigned or UnicodeCategory.Surrogate))throw new BackendError("nameInvalid","");
        return name;
    }
    private async Task SaveGraphics()
    {
        try{LoadSnapshot(await Backend.Run("save",GraphicsRequest(),Receive),acceptGraphics:true);status.Text=Lang.T("saved");}
        catch(BackendError error)when(error.Code=="settings_saved_refresh_failed"){AcceptGraphicsDraft();throw;}
    }
    private async Task Fallback()
    {
        if(!await ResolveGraphicsDraft())return;
        try{LoadSnapshot(await Backend.Run("save",GraphicsRequest(true),Receive),acceptGraphics:true);status.Text=Lang.T("saved");}
        catch(BackendError error)when(error.Code=="settings_saved_refresh_failed"){renderer.SelectedIndex=2;AcceptGraphicsDraft();throw;}
    }
    private async Task Probe(){await Backend.Run("probe",new {},Receive);LoadSnapshot(await Backend.Run("snapshot",new {},Receive));}
    private async Task Verify(){await Backend.Run("verify",new {},Receive);status.Text=Lang.T("verified");}
    private async Task ShowRepair(){forceSetup=true;await ShowSetup();}
    private async Task Cleanup()
    {
        if(MessageBox.Show(this,Lang.T("cleanupConfirm"),Lang.T("app"),MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
        await Backend.Run("cleanup",new {},Receive);LoadSnapshot(await Backend.Run("snapshot",new {},Receive));status.Text=Lang.T("cleanupDone");
    }
    private async Task Diagnostics()
    {
        var value=await Backend.Run("diagnostics",new {},Receive);
        using var dialog=new SaveFileDialog { FileName="S7-Diagnose.json",Filter="JSON (*.json)|*.json" };
        if(dialog.ShowDialog(this)==DialogResult.OK){await File.WriteAllTextAsync(dialog.FileName,JsonSerializer.Serialize(value,new JsonSerializerOptions {WriteIndented=true}));status.Text=Lang.T("reportSaved");}
    }
    private async Task Firewall()
    {
        await Backend.Run("firewall",new {},Receive);
        status.Text=Lang.T("permissionDone");
    }
    private async Task Play(bool lan)
    {
        if(!await ResolveGraphicsDraft())return;
        JsonElement result;
        if(lan)result=await Backend.Run("lan-play",new {player_name=ValidName()},Receive);
        else result=await Backend.Run("play",new {},Receive);
        QueueDirectorySwitch(result);
        gameRunning=false;RestoreWindow();homeTitle.Text=Lang.T("playTitle");homeInfo.Text=Lang.T("playIntro");LoadSnapshot(await Backend.Run("snapshot",new {},Receive));status.Text=Lang.T("ready");
    }
    private void UpdateInternetControls()
    {
        bool direct=internetMode.SelectedIndex==1;
        internetPolicy.Text=Lang.T(direct?"internetDirectInfo":"internetAssistedInfo");
        internetConnection.Text=Lang.T(!internetOperation&&internetGuestChoice.Checked?"internetConnectionFromInvite":
            direct?"internetConnectionDirect":"internetConnectionAuto");
        internetUnavailable.Visible=!internetAvailable;
        internetNext.Visible=internetOperation&&!publicRoomOperation;
        internetNext.Text=Lang.T(internetCreating
            ? internetStage=="hosting"?"internetHostWaiting":internetStage=="playing"?"internetHostPlaying":internetStage=="ready"?"internetHostReady":"internetHostConnecting"
            : internetStage=="hosting"?"internetGuestEnding":internetStage=="playing"?"internetGuestPlaying":internetStage=="ready"?"internetGuestReady":"internetGuestConnecting");
        foreach(var option in new[]{internetHostChoice,internetGuestChoice})
        {
            option.Enabled=!busy;
            option.BackColor=option.Checked?Color.FromArgb(30,91,164):Color.White;
            option.ForeColor=option.Checked?Color.White:SystemColors.ControlText;
        }
        internetEndpoint.Enabled=!busy&&direct;
        internetMode.Enabled=internetPlayer.Enabled=internetJoin.Enabled=!busy;
        if(internetCreate!=null)internetCreate.Enabled=!busy&&internetAvailable;
        if(internetJoinButton!=null)internetJoinButton.Enabled=!busy&&internetAvailable&&!string.IsNullOrWhiteSpace(internetJoin.Text);
        if(internetPaste!=null)internetPaste.Enabled=!busy&&internetAvailable;
        if(internetCopy!=null)internetCopy.Enabled=internetOperation&&!publicRoomOperation&&internetInvite.Text.Length>0;
        if(internetShare!=null)internetShare.Enabled=internetOperation&&!publicRoomOperation&&internetInvite.Text.Length>0;
        if(internetEnd!=null)
        {
            internetEnd.Enabled=internetOperation&&!gameRunning&&!cancelRequested;
            internetEnd.Text=Lang.T(gameRunning?(internetCreating?"internetEndGameHost":"internetEndGameGuest"):
                internetStage=="connecting"?"internetCancel":internetCreating?"internetEnd":"internetLeave");
        }
        foreach(Control control in internetSetupControls)control.Visible=!internetOperation;
        if(internetHostPanel!=null)internetHostPanel.Visible=!internetOperation&&internetHostChoice.Checked;
        if(internetGuestPanel!=null)internetGuestPanel.Visible=!internetOperation&&internetGuestChoice.Checked;
        if(internetAdvancedPanel!=null)internetAdvancedPanel.Visible=!internetOperation&&internetHostChoice.Checked&&internetAdvanced.Checked;
        if(internetEndpointField!=null)internetEndpointField.Visible=direct;
        if(internetInviteField!=null)internetInviteField.Visible=internetOperation&&!publicRoomOperation&&internetInvite.Text.Length>0;
        if(internetShareRow!=null)internetShareRow.Visible=internetOperation&&!publicRoomOperation&&internetInvite.Text.Length>0;
        if(internetCreate!=null)internetCreate.Visible=!internetOperation&&internetHostChoice.Checked;
        if(internetJoinButton!=null)internetJoinButton.Visible=!internetOperation&&internetGuestChoice.Checked;
        if(internetEnd!=null)internetEnd.Visible=internetOperation&&!publicRoomOperation;
        if(internetEndRow!=null)internetEndRow.Visible=!publicRoomOperation;
        internetInvite.Enabled=true;
        discordPublish.Enabled=!busy&&discordAvailable&&discordConnected;discordPublish.Visible=discordAvailable;
        if(discordConnect!=null)discordConnect.Enabled=!busy&&discordAvailable;
        if(discordDisconnect!=null)discordDisconnect.Enabled=!busy&&discordAvailable&&discordConnected;
        if(discordOptionsPanel!=null)discordOptionsPanel.Visible=discordOptions.Checked;
        discordNotice.Text=Lang.T(!discordAvailable?"discordUnavailableInfo":!discordConnected?"discordNotConnected":"directoryPublisherReady");
        UpdateDirectoryControls();
    }
    private void PasteInternetInvitation()
    {
        // Read the clipboard only after the explicit paste action. Never log it.
        try{internetJoin.Text=Community.InvitationCode(Clipboard.GetText());}
        catch(System.Runtime.InteropServices.ExternalException){throw new BackendError("internetClipboardReadFailed","");}
    }
    private async Task ConnectDiscord()
    {
        using var dialog=new Form { Text=Lang.T("discordConnect"),ClientSize=new Size(650,290),
            StartPosition=FormStartPosition.CenterParent,FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false,MinimizeBox=false };
        var explanation=new Label {Text=Lang.T("discordConnectInfo"),Left=18,Top=18,Width=610,Height=142};
        var code=new TextBox {Left=18,Top=172,Width=610,MaxLength=2048,UseSystemPasswordChar=true,AccessibleName=Lang.T("discordConnectionCode")};
        var accept=new Button {Text=Lang.T("discordConnect"),Left=410,Top=226,Width=220,Height=38,DialogResult=DialogResult.OK};
        var cancel=new Button {Text=Lang.T("cancel"),Left=250,Top=226,Width=150,Height=38,DialogResult=DialogResult.Cancel};
        dialog.Controls.AddRange([explanation,code,accept,cancel]);dialog.AcceptButton=accept;dialog.CancelButton=cancel;
        if(dialog.ShowDialog(this)!=DialogResult.OK)return;
        string secret=code.Text;code.Clear();using var channel=new InternetChannel();
        Task pump=channel.Run(secret,_=>{});secret="";
        try{await Backend.Run("discord-connect",new {channel=channel.Name},Receive);}
        finally{channel.Dispose();try{await pump;}catch(Exception){}}
        LoadSnapshot(await Backend.Run("snapshot",new {},Receive));discordOptions.Checked=true;UpdateInternetControls();status.Text=discordNotice.Text;
    }
    private async Task DisconnectDiscord()
    {
        await Backend.Run("discord-disconnect",new {},Receive);
        LoadSnapshot(await Backend.Run("snapshot",new {},Receive));status.Text=Lang.T("discordNotConnected");
    }
    private async Task PlayInternet(bool create,bool publish=false,bool publicLobby=false)
    {
        if(!await ResolveGraphicsDraft())return;
        player.Text=internetPlayer.Text;string name=ValidName();
        string? invitation=create?null:Community.InvitationCode(internetJoin.Text);
        if(!create&&(string.IsNullOrWhiteSpace(invitation)||invitation.Length>2048))throw new BackendError("internet_invitation_invalid","");
        bool direct=internetMode.SelectedIndex==1;
        string? endpoint=direct?internetEndpoint.Text:null;
        if(!ConfirmInternetConnection(publicLobby||publish))return;
        publicRoomOperation=publicLobby||publish;
        internetCreating=create;internetStage="connecting";internetOperation=true;internetInvite.Clear();internetJoin.Clear();SetBusy(true);
        tabs.SelectedTab=publicRoomOperation?directoryPage:internetPage;
        using var channel=new InternetChannel();
        Task privateTask=channel.Run(invitation,code=>
        {
            void Accept(){if(internetOperation){internetInvite.Text=code;internetStage="ready";UpdateInternetControls();}}
            if(InvokeRequired)BeginInvoke(new Action(Accept));else Accept();
        });
        try
        {
            JsonElement result;
            if(create)
            {
                var request=new Dictionary<string,object?> { ["player_name"]=name,["channel"]=channel.Name,["mode"]=direct?"direct":"assisted",
                    ["direct_endpoint"]=endpoint,["publish_discord"]=publish,["internet_consent"]=1 };
                if(publicHostingAvailable)request["public_lobby"]=publicLobby;
                result=await Backend.Run("internet-create",request,Receive);
            }
            else result=await Backend.Run("internet-join",new {player_name=name,channel=channel.Name,publish_discord=false,internet_consent=1},Receive);
            QueueDirectorySwitch(result);
            channel.Dispose();await privateTask;
            gameRunning=false;homeTitle.Text=Lang.T("playTitle");homeInfo.Text=Lang.T("playIntro");RestoreWindow();LoadSnapshot(await Backend.Run("snapshot",new {},Receive));status.Text=Lang.T("ready");
        }
        finally
        {
            channel.Dispose();
            try{await privateTask;}catch(Exception){/* No private bytes enter exception details. */}
            CloseGameInvitation();internetInvite.Clear();internetOperation=false;publicRoomOperation=false;UpdateInternetControls();
        }
    }
    private void Receive(JsonElement value)
    {
        if(InvokeRequired){BeginInvoke(new Action(()=>Receive(value)));return;}
        string? kind=value.GetProperty("kind").GetString();
        if(kind=="directory_switch")
        {
            // The awaited result owns the switch. Progress notifications may
            // arrive later and must never start a connection or leave UI busy.
            status.Text=Lang.T("internet_consent_required");
        }
        else if(kind=="progress")
        {
            string phase=value.GetProperty("phase").GetString()!;long current=value.GetProperty("current").GetInt64(),total=value.GetProperty("total").GetInt64();
            if(internetOperation)
            {
                if(phase=="internet_hosting")internetStage="hosting";
                else if(phase is "internet_ready" or "starting")internetStage="ready";
                UpdateInternetControls();
            }
            if(phase is "patch" or "verify" or "graphics_probe" or "graphics_apply" or "finish")cancelButton.Enabled=false;
            if(cancelRequested){status.Text=Lang.T("cancelRequested");progress.Style=ProgressBarStyle.Marquee;return;}
            status.Text=Lang.T("phase_"+phase)+(phase=="copy"&&total>0?"  "+(current/1073741824.0).ToString("0.0")+" / "+(total/1073741824.0).ToString("0.0")+" GB":"");
            progress.Visible=true;progress.Style=total>0?ProgressBarStyle.Continuous:ProgressBarStyle.Marquee;
            if(total>0)progress.Value=(int)Math.Clamp(current*100/total,0,100);
        }
        else if(kind=="game_started")
        {
            CloseGameInvitation();
            if(value.TryGetProperty("invitation_run",out var run)&&run.ValueKind==JsonValueKind.String)invitationRun=run.GetString();
            gameRunning=true;homeTitle.Text=Lang.T("running");homeInfo.Text=Lang.T("runningInfo");status.Text=Lang.T("running");progress.Visible=false;
            if(internetOperation)internetStage="playing";UpdateInternetControls();
        }
        else if(kind=="game_finished")
        {
            CloseGameInvitation();
            gameRunning=false;homeTitle.Text=Lang.T("playTitle");homeInfo.Text=Lang.T("playIntro");
            if(internetOperation)internetStage="hosting";status.Text=Lang.T("ready");UpdateInternetControls();
        }
        else if(kind=="invitation_requested")OpenGameInvitation(value);
        else if(kind=="internet_policy")
        {
            internetMode.SelectedIndex=value.GetProperty("mode").GetString()=="direct"?1:0;
            UpdateInternetControls();
        }
        else if(kind=="warning")
        {
            string code=value.GetProperty("code").GetString()!;status.Text=Lang.T(code);
            tray.BalloonTipTitle=Lang.T("app");tray.BalloonTipText=Lang.T(code);tray.Visible=true;tray.ShowBalloonTip(10000);
        }
    }
    private void SetBusy(bool value)
    {
        if(value)InvalidateDirectoryRefresh();
        busy=value;tabs.Enabled=!value||internetOperation;source.Enabled=destination.Enabled=installButton.Enabled=!value;language.Enabled=!value;
        foreach(TabPage page in tabs.TabPages)page.Enabled=!value||(internetOperation&&page==(publicRoomOperation?directoryPage:internetPage));
        foreach(var button in Descendants(setup).OfType<Button>())button.Enabled=!value;
        cancelButton.Visible=value&&setup.Visible;cancelButton.Enabled=true;
        if(value){status.Text=Lang.T("working");progress.Visible=true;progress.Style=ProgressBarStyle.Marquee;}
        else
        {
            cancelRequested=false;progress.Visible=false;DisplayEnabled();if(setup.Visible)installButton.Enabled=recoverState||source.SelectedIndex>=0;
            if(status.Text==Lang.T("working"))status.Text=Lang.T(gameRunning?"running":"ready");
        }
        UpdateInternetControls();
        RefreshUpdateControls();
        UpdateDirectoryControls();
        UpdateGraphicsDraft();
    }
    private static IEnumerable<Control> Descendants(Control parent)
    { foreach(Control control in parent.Controls){yield return control;foreach(var child in Descendants(control))yield return child;} }
    private async Task Guard(Func<Task> action,bool showErrors=true)
    {
        if(busy)return;
        SetBusy(true);
        try{await action();}
        catch(Exception error)
        {
            if(showErrors){gameRunning=false;RestoreWindow();ShowError(error);}
            else status.Text=Lang.T(error is BackendError backend?backend.Code:"operation_failed");
        }
        finally
        {
            SetBusy(false);
        }
        if(pendingDirectoryGame is string pending)
        {
            pendingDirectoryGame=null;RestoreWindow();
            await Guard(()=>JoinDirectoryGame(pending));
        }
    }
    private void ShowError(Exception error)
    {
        string code=error is BackendError backend?backend.Code:"operation_failed";lastError=error.Message;details.Text=lastError;
        status.Text=Lang.T(code);if(!testing)MessageBox.Show(this,Lang.T(code),Lang.T("errorTitle"),MessageBoxButtons.OK,MessageBoxIcon.Warning);
    }
    private void RestoreWindow(){ShowInTaskbar=true;WindowState=FormWindowState.Normal;tray.Visible=false;Show();}

    internal static int SelfTest(string folder)
    {
        Directory.CreateDirectory(folder);
        var menuLayouts=new List<object>();
        try
        {
            string? pipeProbe=Environment.GetEnvironmentVariable("S7_TEST_PRIVATE_PROBE");
            string? testPython=Environment.GetEnvironmentVariable("S7_TEST_PYTHON");
            if(pipeProbe!=null&&testPython!=null)
            {
                Task.Run(async()=>
                {
                    using var channel=new InternetChannel();string received="";
                    Task pump=channel.Run(null,value=>received=value);
                    var start=new ProcessStartInfo(testPython){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
                    foreach(string argument in new[]{"-I","-B",pipeProbe,"--channel",channel.Name})start.ArgumentList.Add(argument);
                    using var client=Process.Start(start)??throw new Exception("Private channel client failed");
                    await client.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(15));
                    await pump.WaitAsync(TimeSpan.FromSeconds(5));
                    string output=await client.StandardOutput.ReadToEndAsync();
                    if(client.ExitCode!=0||received!="weplay1.synthetic-pipe-fixture"||output.Contains("weplay1"))throw new Exception("Private channel round trip failed");
                }).GetAwaiter().GetResult();
            }
            if(!Lang.Read("en").Keys.ToHashSet().SetEquals(Lang.Read("de").Keys))throw new Exception("Translation keys differ");
            foreach(string locale in new[]{"en","de"})
                if(Lang.Read(locale).Values.Any(text=>text.Contains("\u00c3")||text.Contains("\u00c2")||text.Contains("\ufffd")))
                    throw new Exception("Translation encoding damaged: "+locale);
            string fixture="""
            {"installed":true,"updates_supported":true,"displays":[{"device":"\\\\.\\DISPLAY1","adapter":"Fixture GPU","primary":true,"x":0,"y":0,"width":1920,"height":1080,"resolutions":[[1280,720],[1920,1080]]}],"data_default":"C:\\Fixture\\Instances","source":"C:\\Fixture\\Original","renderer_policy":"auto","probe":{"devices":["Fixture GPU"],"failure":null},"choice":{"format":"s7-graphics-choice-v1","choice":"Fixture GPU","simple_menu":true,"display":{"device":"\\\\.\\DISPLAY1","width":1920,"height":1080,"mode":"borderless"}},"quality":{"ShaderQuality":1,"TextureQuality":1,"ObjectQuality":1,"DistanceCulling":1,"ShadowQuality":1,"WaterQuality":1,"ClutterQuality":1,"Shadows":1,"SSAO":0,"DepthOfField":0,"SunShafts":0,"Parallax":0,"Antialiasing":1},"player_name":"Tester","renderer":"dxvk","current":"C:\\Fixture\\Game","update_available":false}
            """;
            using var json=JsonDocument.Parse(fixture);
            foreach(string language in new[]{"en","de"})
            {
                Lang.Set(language,false);
                foreach(bool publicLobby in new[]{false,true})
                {
                    var consent=BuildInternetConsent(publicLobby);using var dialog=consent.Dialog;
                    if(consent.Choice.Checked||consent.Connect.Enabled)throw new Exception("EasyTier consent must start unchecked and disabled");
                    consent.Choice.Checked=true;if(!consent.Connect.Enabled)throw new Exception("Explicit consent did not enable connect");
                    consent.Choice.Checked=false;if(consent.Connect.Enabled)throw new Exception("Revoked consent left connect enabled");
                    dialog.StartPosition=FormStartPosition.Manual;dialog.Location=new Point(-30000,-30000);dialog.ShowInTaskbar=false;
                    dialog.Show();Application.DoEvents();using var consentImage=new Bitmap(dialog.Width,dialog.Height);
                    dialog.DrawToBitmap(consentImage,new Rectangle(0,0,dialog.Width,dialog.Height));
                    consentImage.Save(Path.Combine(folder,language+(publicLobby?"-public-consent.png":"-private-consent.png")));dialog.Close();
                }
                Lang.Set(language,false);using var form=new MainForm(false,true);form.StartPosition=FormStartPosition.Manual;form.Location=new Point(-30000,-30000);form.ShowInTaskbar=false;
                if(form.releaseVersion.Text!=Program.ReleaseVersion||!form.releaseVersion.Text.StartsWith("2."))throw new Exception("Release version label differs from assembly metadata");
                form.LoadSnapshot(json.RootElement,acceptGraphics:true);form.ShowMain();form.Show();Application.DoEvents();
                form.TestLauncherResilience(json.RootElement);
                form.TestGameInvitation(folder,language);
                form.tabs.SelectedTab=form.homePage;Application.DoEvents();
                if(form.playModes.Count!=4||!form.playModes.Select(x=>x.Card.Mode).SequenceEqual(Enum.GetValues<PlayMode>()))
                    throw new Exception("Four distinct play modes are required");
                foreach(var entry in form.playModes)
                {
                    var cell=form.modeGrid.GetCellPosition(entry.Card);
                    if(cell.Column!=(int)entry.Card.Mode%2||cell.Row!=(int)entry.Card.Mode/2||!entry.Card.TextFits())
                        throw new Exception("Play mode card layout or translated text does not fit: "+entry.Card.Mode);
                    if(!entry.Card.TabStop||entry.Card.AccessibleRole!=AccessibleRole.PushButton||
                        entry.Card.AccessibleName!=entry.Card.Text||string.IsNullOrWhiteSpace(entry.Card.AccessibleDescription))
                        throw new Exception("Play mode card lost keyboard/accessibility semantics");
                }
                foreach(var route in new[]{(PlayMode.Lan,form.lanPage),(PlayMode.PrivateInternet,form.internetPage),(PlayMode.PublicInternet,form.directoryPage)})
                {
                    form.playModes.Single(x=>x.Card.Mode==route.Item1).Card.PerformClick();Application.DoEvents();
                    if(form.tabs.SelectedTab!=route.Item2||form.internetOperation||form.publicRoomOperation)
                        throw new Exception("Mode card routed to the wrong page or joined a network");
                    form.tabs.SelectedTab=form.homePage;
                }
                using var request=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest()));
                var output=request.RootElement.GetProperty("choice").GetProperty("display");
                if(output.GetProperty("mode").GetString()!="borderless"||output.GetProperty("width").GetInt32()!=1920||form.resolution.Enabled)throw new Exception("Borderless controls failed");
                if(form.anisotropicFiltering.Checked)throw new Exception("Legacy preferences force texture filtering");
                if(form.extendedDetail.Checked||form.detailedShadows.Checked||form.extendedShadowRange.Checked)throw new Exception("Legacy preferences force visual detail");
                if(form.extendedDetail.Enabled||form.detailedShadows.Enabled||form.extendedShadowRange.Enabled)throw new Exception("Unsupported adapter exposes visual controls");
                form.visualDetailAvailable=true;form.DisplayEnabled();
                form.extendedDetail.Checked=true;form.detailedShadows.Checked=true;form.extendedShadowRange.Checked=true;
                using(var detailed=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest())))
                {
                    var detailChoice=detailed.RootElement.GetProperty("choice");
                    if(!detailChoice.GetProperty("extended_detail").GetBoolean()||!detailChoice.GetProperty("detailed_shadows").GetBoolean()||!detailChoice.GetProperty("extended_shadow_range").GetBoolean())
                        throw new Exception("Visual detail choices lost in request");
                }
                form.midsummerToneAvailable=true;form.renderer.SelectedIndex=1;form.DisplayEnabled();form.midsummerTone.Checked=true;
                form.enhancedAa.Checked=true;form.contactShadows.Checked=true;form.ultraShadows.Checked=true;form.maximumFx.Checked=true;
                using(var tone=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest())))
                    if(!form.midsummerTone.Enabled||!form.enhancedAa.Enabled||!form.contactShadows.Enabled
                        ||!tone.RootElement.GetProperty("choice").GetProperty("midsummer_tone").GetBoolean()
                        ||!tone.RootElement.GetProperty("choice").GetProperty("enhanced_aa").GetBoolean()
                        ||!tone.RootElement.GetProperty("choice").GetProperty("contact_shadows").GetBoolean()
                        ||!tone.RootElement.GetProperty("choice").GetProperty("ultra_shadows").GetBoolean()
                        ||!tone.RootElement.GetProperty("choice").GetProperty("maximum_fx").GetBoolean())throw new Exception("Visual quality choice lost");
                form.midsummerTone.Checked=form.enhancedAa.Checked=form.contactShadows.Checked=form.ultraShadows.Checked=form.maximumFx.Checked=false;
                form.extendedDetail.Checked=false;form.detailedShadows.Checked=false;form.extendedShadowRange.Checked=false;
                form.ApplyQualityPreset(false);
                using(var preset=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest())))
                {
                    var values=preset.RootElement.GetProperty("quality");
                    if(values.GetProperty("DepthOfField").GetInt32()!=0||values.GetProperty("ObjectQuality").GetInt32()!=1||
                        values.GetProperty("TextureQuality").GetInt32()!=2||!preset.RootElement.GetProperty("choice").GetProperty("anisotropic_filtering").GetBoolean())
                        throw new Exception("Clear landscape preset failed");
                }
                form.ApplyQualityPreset(true);
                if(form.qualities["ObjectQuality"].SelectedIndex!=2)throw new Exception("Object detail preset failed");
                form.visualDetailAvailable=true;form.midsummerToneAvailable=true;
                form.ApplyRecommendedGraphics();
                using(var recommended=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest())))
                {
                    var choice=recommended.RootElement.GetProperty("choice");
                    if(recommended.RootElement.GetProperty("renderer_policy").GetString()!="dxvk"
                        ||choice.GetProperty("ultra_shadows").GetBoolean()||choice.GetProperty("maximum_fx").GetBoolean()
                        ||choice.GetProperty("midsummer_tone").GetBoolean()||choice.GetProperty("contact_shadows").GetBoolean()
                        ||!new[]{"extended_detail","detailed_shadows","extended_shadow_range","enhanced_aa"}.All(name=>choice.GetProperty(name).GetBoolean()))
                        throw new Exception("Recommended graphics profile failed");
                }
                form.LoadSnapshot(json.RootElement,acceptGraphics:true);
                form.renderer.SelectedIndex=2;
                using var compatible=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest()));
                if(compatible.RootElement.GetProperty("choice").GetProperty("choice").ValueKind!=JsonValueKind.Null)throw new Exception("Native GPU filtering enabled");
                if(form.anisotropicFiltering.Enabled||form.midsummerTone.Enabled||form.midsummerTone.Checked
                    ||form.enhancedAa.Enabled||form.contactShadows.Enabled||form.maximumFx.Enabled||form.maximumFx.Checked||form.enhancedAa.Checked||form.contactShadows.Checked)throw new Exception("Native renderer exposes Modern-only filtering");
                form.visualDetailAvailable=true;form.DisplayEnabled();
                if(!form.extendedDetail.Enabled||!form.detailedShadows.Enabled||!form.extendedShadowRange.Enabled||!form.ultraShadows.Enabled)throw new Exception("Native visual detail controls unavailable");
                form.tabs.SelectedTab=form.internetPage;form.internetAvailable=true;form.discordAvailable=true;form.UpdateInternetControls();Application.DoEvents();
                void Capture(string name)
                {
                    Application.DoEvents();using var image=new Bitmap(form.Width,form.Height);
                    form.DrawToBitmap(image,new Rectangle(0,0,form.Width,form.Height));image.Save(Path.Combine(folder,language+"-"+name+".png"));
                }
                if(!form.internetHostPanel.Visible||form.internetGuestPanel.Visible||form.internetAdvancedPanel.Visible||form.discordOptionsPanel.Visible||!form.internetCreate.Enabled)
                    throw new Exception("Internet host defaults failed");
                Capture("internet-host");
                form.internetAdvanced.Checked=true;form.internetMode.SelectedIndex=1;Application.DoEvents();
                if(!form.internetAdvancedPanel.Visible||!form.internetEndpointField!.Visible)throw new Exception("Direct connection disclosure failed");
                Capture("internet-advanced");
                form.internetGuestChoice.Checked=true;Application.DoEvents();
                if(form.internetHostPanel.Visible||!form.internetGuestPanel.Visible||form.internetAdvancedPanel.Visible||form.internetJoinButton.Enabled||form.internetConnection.Text!=Lang.T("internetConnectionFromInvite"))
                    throw new Exception("Internet guest choice failed");
                form.internetJoin.Text="weplay1.synthetic-pipe-fixture";
                if(!form.internetJoinButton.Enabled)throw new Exception("Guest invitation did not enable join");
                Capture("internet-guest");
                form.internetHostChoice.Checked=true;form.internetAdvanced.Checked=false;form.internetMode.SelectedIndex=0;
                form.SetBusy(true);form.SetBusy(false);
                if(form.status.Text!=Lang.T("ready"))throw new Exception("Navigation leaves a stale busy message");
                form.tabs.SelectedTab=form.directoryPage;
                if(Descendants(form.internetPage).Contains(form.discordOptions)||!Descendants(form.directoryPage).Contains(form.discordOptions))
                    throw new Exception("Public publishing controls mixed into private invitation tab");
                form.discordOptions.Checked=true;Application.DoEvents();
                if(form.discordPublish.Enabled||!form.discordConnect.Enabled||!form.discordOptionsPanel.Visible||form.discordNotice.Text!=Lang.T("discordNotConnected"))
                    throw new Exception("Public host access controls failed");
                Capture("public-host-options");form.discordOptions.Checked=false;form.tabs.SelectedTab=form.internetPage;
                form.internetCreating=true;form.internetStage="connecting";form.internetOperation=true;form.internetInvite.Text="weplay1.synthetic-pipe-fixture";form.SetBusy(true);
                if(!form.internetCopy.Enabled||!form.internetShare.Enabled||!form.discordLink.Enabled||!form.internetInvite.UseSystemPasswordChar||form.internetCreate.Enabled||!form.internetPage.Enabled||form.lanPage.Enabled)
                    throw new Exception("Private Internet room controls failed");
                string share=Community.InvitationMessage("weplay1.synthetic-pipe-fixture",language);
                if(Community.InvitationCode(share)!="weplay1.synthetic-pipe-fixture"||!share.Contains(Community.DiscordUrl)||Community.DiscordCode!="j2eQgVzssc")
                    throw new Exception("Discord sharing failed");
                foreach(string invalid in new[]{Community.DiscordUrl,"https://example.invalid/#weplay1.secret","text\nweplay1.secret","weplay1.secret\nuntrusted"})
                {try{Community.InvitationCode(invalid);throw new Exception("Unsafe invitation accepted");}catch(BackendError){}}
                using(var started=JsonDocument.Parse("""{"kind":"game_started"}"""))form.Receive(started.RootElement);
                if(form.internetEnd.Enabled||form.internetNext.Text!=Lang.T("internetHostPlaying")||form.internetHostPanel.Visible||form.internetGuestPanel.Visible)
                    throw new Exception("Running host guidance or room-close guard failed");
                Capture("internet-room");
                using(var finished=JsonDocument.Parse("""{"kind":"game_finished"}"""))form.Receive(finished.RootElement);
                if(!form.internetEnd.Enabled||form.internetNext.Text!=Lang.T("internetHostWaiting")||form.internetEnd.Text!=Lang.T("internetEnd"))
                    throw new Exception("Host room waiting guidance failed");
                form.internetCreating=false;form.internetInvite.Clear();
                using(var started=JsonDocument.Parse("""{"kind":"game_started"}"""))form.Receive(started.RootElement);
                if(form.internetEnd.Enabled||form.internetNext.Text!=Lang.T("internetGuestPlaying")||form.internetInviteField!.Visible)
                    throw new Exception("Running guest guidance failed");
                Capture("internet-guest-playing");
                using(var finished=JsonDocument.Parse("""{"kind":"game_finished"}"""))form.Receive(finished.RootElement);
                if(form.internetEnd.Text!=Lang.T("internetLeave"))throw new Exception("Guest close label failed");
                form.internetInvite.Clear();form.internetOperation=false;form.SetBusy(false);
                form.tabs.SelectedTab=form.directoryPage;
                form.discordAvailable=true;form.discordConnected=true;form.discordPublish.Checked=true;form.UpdateInternetControls();
                if(!form.discordPublish.Enabled||!form.discordDisconnect.Enabled||!form.discordConnect.Enabled)
                    throw new Exception("Connected Discord publisher controls failed");
                form.discordPublish.Checked=false;
                if(form.discordNotice.Text!=Lang.T("directoryPublisherReady"))throw new Exception("Public host access status is unclear");
                form.discordAvailable=false;form.UpdateInternetControls();
                if(form.discordConnect.Enabled||form.discordPublish.Visible||form.discordNotice.Text!=Lang.T("discordUnavailableInfo"))
                    throw new Exception("Unavailable Discord capability controls failed");
                form.discordAvailable=true;form.UpdateInternetControls();
                form.directoryAvailable=true;
                form.publicHostingAvailable=true;form.discordConnected=false;form.UpdateInternetControls();
                using(var directory=JsonDocument.Parse("""{"connected":true,"games":[{"id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","title":"Community game","state":"open","players":1,"capacity":4,"free_slots":3,"version":"2.0.0-rc3.12","compatible":true}]}"""))
                    form.ShowDirectory(directory.RootElement,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
                form.tabs.SelectedTab=form.directoryPage;Application.DoEvents();form.UpdateDirectoryControls();
                if(!form.directoryJoin.Enabled)throw new Exception("Compatible open directory game cannot be selected");
                if(!form.directoryHost.Enabled||form.discordPublish.Checked||form.discordPublish.Enabled||!form.discordPublish.Visible)
                    throw new Exception("Public hosting requires Discord or silently enables Discord publication");
                Capture("public-no-discord");
                form.publicRoomOperation=true;form.internetOperation=true;form.internetStage="ready";
                form.internetInvite.Text="weplay1.synthetic-private-bytes";form.SetBusy(true);Application.DoEvents();
                if(!form.directoryPage.Enabled||form.internetPage.Enabled||!form.directoryProgress.Visible||form.internetCopy.Enabled||form.internetShare.Enabled||form.internetInviteField!.Visible)
                    throw new Exception("Public room operation leaked private invitation UI");
                Capture("public-room-connected");form.publicRoomOperation=false;form.internetOperation=false;form.internetInvite.Clear();form.SetBusy(false);
                form.SetBusy(true);if(form.directoryJoin.Enabled)throw new Exception("Directory join enabled during game operation");form.SetBusy(false);
                foreach(string row in new[]{"""{"compatible":false,"state":"open","free_slots":3}""","""{"compatible":true,"state":"full","free_slots":0}""","""{"compatible":true,"state":"started","free_slots":0}"""})
                {using var unavailable=JsonDocument.Parse(row);if(DirectoryJoinable(unavailable.RootElement))throw new Exception("Unavailable directory game enabled");}
                using(var switched=JsonDocument.Parse("""{"kind":"directory_switch","game_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}"""))form.Receive(switched.RootElement);
                if(form.busy||form.internetOperation||form.pendingDirectoryGame!=null)throw new Exception("Progress notification bypassed per-room consent");
                using(var completed=JsonDocument.Parse("""{"exit_code":0,"directory_game_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}"""))form.QueueDirectorySwitch(completed.RootElement);
                if(form.pendingDirectoryGame!="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")throw new Exception("Awaited room switch result lost");
                form.pendingDirectoryGame=null;
                form.status.Text=Lang.T("ready");
                foreach(var page in new[]{form.homePage,form.lanPage,form.internetPage,form.directoryPage,form.graphicsPage,form.helpPage,form.updatesPage})
                {
                    form.tabs.SelectedTab=page;Application.DoEvents();form.FitMenuHeight();Application.DoEvents();
                    var scroll=page.Controls.OfType<Panel>().Single(panel=>panel.AutoScroll);
                    int overflow=scroll.DisplayRectangle.Height-scroll.ClientSize.Height;
                    int available=Screen.FromControl(form).WorkingArea.Height-16;
                    if(overflow>0&&form.Height<available)throw new Exception("Menu has unused vertical screen space");
                    menuLayouts.Add(new {language,page=form.tabs.SelectedIndex,height=form.Height,available,overflow});
                    using var bitmap=new Bitmap(form.Width,form.Height);
                    form.DrawToBitmap(bitmap,new Rectangle(0,0,form.Width,form.Height));bitmap.Save(Path.Combine(folder,language+"-"+form.tabs.SelectedIndex+".png"));
                }
                form.tabs.SelectedTab=form.graphicsPage;form.renderer.SelectedIndex=0;form.ApplyQualityPreset(false);
                var graphicsScroll=form.graphicsPage.Controls.OfType<Panel>().Single(panel=>panel.AutoScroll);
                form.graphicsQualityDetails.Checked=true;form.graphicsEffectDetails.Checked=true;Application.DoEvents();
                graphicsScroll.ScrollControlIntoView(form.qualities["ShaderQuality"]);Capture("graphics-quality");
                graphicsScroll.ScrollControlIntoView(form.anisotropicFiltering);Capture("graphics-effects");
                form.visualDetailAvailable=true;form.DisplayEnabled();
                graphicsScroll.ScrollControlIntoView(form.simple);Capture("graphics-detail");
                Size normalSize=form.Size;form.automaticMenuHeight=false;form.Size=form.MinimumSize;
                foreach(var page in new[]{form.homePage,form.lanPage,form.internetPage,form.directoryPage,form.graphicsPage})
                {
                    form.tabs.SelectedTab=page;Capture("minimum-"+form.tabs.SelectedIndex);form.AssertPrimaryActionVisible(page);
                    if(page==form.homePage)
                    {
                        var viewport=(Panel)page.Controls[0];
                        foreach(var entry in form.playModes)
                            if(!viewport.ClientRectangle.Contains(viewport.RectangleToClient(entry.Card.RectangleToScreen(entry.Card.ClientRectangle))))
                                throw new Exception("A play mode requires scrolling at the minimum window size: "+entry.Card.Mode);
                    }
                }
                if(form.playModes.Any(x=>!x.Card.TextFits()))throw new Exception("Mode card text clips at the minimum window width");
                form.Size=normalSize;form.automaticMenuHeight=true;
                form.tabs.SelectedTab=form.helpPage;var helpScroll=(Panel)form.helpPage.Controls[0];helpScroll.AutoScrollPosition=new Point(0,helpScroll.DisplayRectangle.Height);Application.DoEvents();
                using(var supportImage=new Bitmap(form.Width,form.Height))
                {form.DrawToBitmap(supportImage,new Rectangle(0,0,form.Width,form.Height));supportImage.Save(Path.Combine(folder,language+"-help-community.png"));}
                form.updatesSupported=true;
                using(var offered=JsonDocument.Parse("""{"available":true,"version":"2.0.0-rc3.5","sequence":2,"notes":"UI fixture","reason":"available"}"""))form.ShowUpdateResult(offered.RootElement);
                form.tabs.SelectedTab=form.homePage;Application.DoEvents();
                if(!form.homeUpdateInstall.Visible||!form.homeUpdateInstall.Enabled||!form.homeUpdateNotice.Text.Contains("2.0.0-rc3.5"))throw new Exception("Update notice is not visible on the home page");
                form.SetBusy(true);if(form.homeUpdateInstall.Enabled)throw new Exception("Update enabled during another operation");form.SetBusy(false);
                using(var updateImage=new Bitmap(form.Width,form.Height))
                {form.DrawToBitmap(updateImage,new Rectangle(0,0,form.Width,form.Height));updateImage.Save(Path.Combine(folder,language+"-update-found.png"));}
                form.tabs.Visible=false;form.setup.Visible=true;form.source.Items.Add(new Item("C:\\Fixture\\Original","C:\\Fixture\\Original"));form.source.SelectedIndex=0;form.setupNotice.Text=Lang.T("found");Application.DoEvents();
                using var setupImage=new Bitmap(form.Width,form.Height);form.DrawToBitmap(setupImage,new Rectangle(0,0,form.Width,form.Height));setupImage.Save(Path.Combine(folder,language+"-setup.png"));
                form.DiscardGraphicsDraft();form.Close();
                TestPrimaryActionScaling(json.RootElement,folder,language);
            }
            File.WriteAllText(Path.Combine(folder,"result.json"),JsonSerializer.Serialize(new {
                passed=true,checks=new[]{"translation_keys","release_version_from_assembly","borderless_native_resolution","native_gpu_filter_disabled",
                    "graphics_presets_and_filtering","bilingual_pages_rendered","internet_role_choices_and_disclosure","navigation_ready_status","invitation_join_guard","internet_game_lifecycle_guidance",
                    "private_internet_controls","discord_community_and_share","discord_publisher_opt_in","minimum_window_pages_rendered","menus_fit_or_screen_height_limit","home_update_notice_and_busy_guard",
                    "easytier_consent_defaults_denied","room_switch_requires_fresh_consent","four_play_mode_cards","mode_card_navigation_without_network_join",
                    "mode_card_accessibility_and_text_fit","four_modes_visible_at_minimum_size","public_hosting_without_discord_and_optional_listing",
                    "ingame_invitation_run_and_role_guards","invitation_clipboard_failure_and_private_public_separation",
                    "graphics_draft_save_discard_cancel","hardware_refresh_preserves_draft","local_refresh_stale_and_obsolete_results",
                    "language_and_cancel_io_error_boundaries","primary_actions_visible_at_minimum_size",
                    "primary_actions_at_125_150_percent_geometry_and_text_scale"},menu_layouts=menuLayouts,private_pipe_roundtrip=pipeProbe!=null&&testPython!=null}));return 0;
        }
        catch(Exception error){File.WriteAllText(Path.Combine(folder,"failure.txt"),error.ToString());return 2;}
    }
}

