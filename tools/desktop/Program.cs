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
    internal static string DisplayVersion(string version)
    {
        string value=version.Split('+')[0];
        if(value=="2.0.0-rc4.0")return "rc4.00";
        if(value=="2.0.0-rc4.1")return "rc4.01";
        if(value=="2.0.0-rc4.2")return "rc4.02";
        if(value=="2.0.0-rc4.3")return "rc4.03";
        if(value=="2.0.0-rc4.4")return "rc4.04";
        if(value=="2.0.0-rc4.5")return "rc4.05";
        if(value=="2.0.0-rc4.6")return "rc4.06";
        if(value=="2.0.0-rc4.7")return "rc4.07";
        if(value=="2.0.0-rc4.8")return "rc4.08";
        if(value=="2.0.0-rc4.9")return "rc4.09";
        int marker=value.IndexOf("-rc",StringComparison.OrdinalIgnoreCase);
        return marker>=0?value[(marker+1)..].ToLowerInvariant():value;
    }
    internal static string ReleaseVersion => DisplayVersion(Assembly.GetExecutingAssembly()
        .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0]
        ?? Assembly.GetExecutingAssembly().GetName().Version?.ToString() ?? "");
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
    private LinkLabel startupDiagnosticsLink=null!;
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
    private readonly TextBox setupDetails = new() { Multiline = true, ReadOnly = true, Height = 108,
        ScrollBars = ScrollBars.Vertical, Visible = false };
    private Button setupHelp = null!;
    private string? setupErrorCode;
    private readonly Button installButton, cancelButton;
    private readonly CheckBox setupNetwork = new() { AutoSize = true, MaximumSize = new Size(820,0) };
    private readonly Label homeTitle = new() { AutoSize = true, Font = new Font("Segoe UI",22,FontStyle.Bold) };
    private readonly Label homeInfo = new() { AutoSize = true, MaximumSize = new Size(850,0) };
    private readonly TextBox player = new() { Dock = DockStyle.Fill, MaxLength = 24 };
    private const int DefaultLanDiscoveryPort=9112;
    private readonly NumericUpDown lanDiscoveryPort = new() {Minimum=1024,Maximum=65535,Value=DefaultLanDiscoveryPort,Width=110};
    private int savedLanDiscoveryPort=DefaultLanDiscoveryPort;
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
    private readonly Dictionary<string,string> windowedResolutions=new(StringComparer.Ordinal);
    private bool rebuildingResolutions;
    private readonly CheckBox keepDisplay = new() { AutoSize = true }, simple = new() { AutoSize = true };
    private readonly CheckBox anisotropicFiltering = new() { AutoSize = true };
    private readonly TrackBar detailLevel = new() { Minimum=0,Maximum=1,TickFrequency=1,LargeChange=1,SmallChange=1 };
    private static readonly int[] detailPercentages=[0,50,100,150,200,400];
    private readonly CheckBox linkShadowRange = new() { AutoSize = true };
    private readonly CheckBox detailedShadows = new() { AutoSize = true };
    private readonly CheckBox extendedShadowRange = new() { AutoSize = true };
    private readonly CheckBox midsummerTone = new() { AutoSize = true };
    private readonly ComboBox antialiasing = Combo();
    private readonly CheckBox contactShadows = new() { AutoSize = true };
    private readonly CheckBox ultraShadows = new() { AutoSize = true };
    private readonly CheckBox maximumFx = new() { AutoSize = true };
    private string RendererMode => (renderer.SelectedItem as Item)?.Key ?? "dxvk";

    private void SetRendererModes(IEnumerable<string> modes, string selected)
    {
        renderer.Items.Clear();
        foreach (string mode in modes)
        {
            string? label=mode switch { "dxvk"=>"rendererDxvk", "async"=>"rendererAsync",
                "gplasync"=>"rendererGplAsync", "native"=>"rendererNative", _=>null };
            if(label!=null)renderer.Items.Add(new Item(mode,Lang.T(label)));
        }
        renderer.SelectedIndex=renderer.Items.Count>0?0:-1;
        for(int index=1;index<renderer.Items.Count;index++)
            if(renderer.Items[index] is Item item&&item.Key==selected)renderer.SelectedIndex=index;
    }
    private readonly Dictionary<string,ComboBox> qualities = [];
    private readonly Dictionary<string,CheckBox> effects = [];
    private readonly Dictionary<Control,string> accessibleFields = [];
    private readonly TextBox details = new() { Multiline = true, ReadOnly = true, Dock = DockStyle.Top, Height = 120, ScrollBars = ScrollBars.Vertical };
    private readonly NotifyIcon tray = new() { Icon = SystemIcons.Application, Visible = false };
    private readonly ContextMenuStrip trayMenu = new();
    private readonly ToolStripMenuItem trayRestore = new(), trayExit = new();
    private TabPage homePage = null!, lanPage = null!, internetPage = null!, graphicsPage = null!, helpPage = null!,updatesPage=null!;
    private bool automaticMenuHeight = true;
    private bool singlePlayerOnly;
    private bool compatSmaaAvailable;
    private bool suspendedSmaa,changingAntialiasingChoices;
    private bool AdvancedReshadeAvailable=>RendererMode=="dxvk"&&midsummerToneAvailable;
    private bool CanUseSmaa(string renderer)=>midsummerToneAvailable&&(renderer=="dxvk"||
        (compatSmaaAvailable&&renderer is ("async" or "gplasync")));
    private bool SmaaAvailable=>CanUseSmaa(RendererMode);
    private bool MaximumGraphicsAvailable=>AdvancedReshadeAvailable&&!singlePlayerOnly;
    private string AntialiasingMode=>(antialiasing.SelectedItem as Item)?.Key??"off";

    private void SetAntialiasingMode(string mode)
    {
        changingAntialiasingChoices=true;
        try
        {
            antialiasing.Items.Clear();
            antialiasing.Items.Add(new Item("off",Lang.T("antialiasingOff")));
            antialiasing.Items.Add(new Item("game",Lang.T("antialiasingGame")));
            if(SmaaAvailable)antialiasing.Items.Add(new Item("smaa",Lang.T("antialiasingSmaa")));
            antialiasing.SelectedIndex=mode=="off"?0:mode=="smaa"&&SmaaAvailable?2:1;
        }
        finally{changingAntialiasingChoices=false;}
    }
    private Button openGraphics = null!;

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
        var footer = new TableLayoutPanel { Dock = DockStyle.Bottom, Height = 50, Padding = new Padding(22,2,22,2),ColumnCount=5,RowCount=2 };
        footer.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));footer.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));footer.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        footer.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        footer.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        footer.RowStyles.Add(new RowStyle(SizeType.AutoSize));footer.RowStyles.Add(new RowStyle(SizeType.Percent,100));
        discordLink=new LinkLabel { Dock=DockStyle.Fill, AutoSize=true, TextAlign=ContentAlignment.MiddleRight,
            AccessibleDescription=Community.DiscordUrl };
        var discordCommunity=new TableLayoutPanel { AutoSize=true,Dock=DockStyle.Fill,ColumnCount=2,Margin=new Padding(10,0,0,0) };
        discordCommunity.Controls.Add(new PictureBox { Image=discordSymbol,Size=new Size(22,22),SizeMode=PictureBoxSizeMode.CenterImage,
            Anchor=AnchorStyles.Left,Margin=Padding.Empty },0,0);
        discordCommunity.Controls.Add(discordLink,1,0);
        Bind(discordLink,"discordOpen");discordLink.LinkClicked+=(_,_)=>
        {try{Community.OpenDiscord();}catch(BackendError error){status.Text=Lang.T(error.Code);}};
        var coffeeLink=new LinkLabel { Text="Buy me a coffee",AutoSize=true,Dock=DockStyle.Fill,
            TextAlign=ContentAlignment.MiddleLeft,Image=coffeeSymbol,ImageAlign=ContentAlignment.MiddleLeft,
            Padding=new Padding(30,4,10,4),Margin=new Padding(16,3,0,3),BackColor=Color.FromArgb(255,221,0),
            LinkColor=Color.FromArgb(13,12,34),ActiveLinkColor=Color.FromArgb(13,12,34),
            VisitedLinkColor=Color.FromArgb(13,12,34),AccessibleDescription=Community.CoffeeUrl };
        coffeeLink.LinkClicked+=(_,_)=>
        {try{Community.OpenCoffee();}catch(BackendError error){status.Text=Lang.T(error.Code);}};
        globalUpdateLink=new LinkLabel { AutoSize=true,Dock=DockStyle.Fill,TextAlign=ContentAlignment.MiddleRight,Padding=new Padding(12,0,0,0),Visible=false };
        globalUpdateLink.LinkClicked+=async (_,_)=>await Guard(InstallUpdate);
        startupDiagnosticsLink=new LinkLabel {AutoSize=true,Visible=false,Anchor=AnchorStyles.Left};
        Bind(startupDiagnosticsLink,"diagnostics");startupDiagnosticsLink.LinkClicked+=async (_,_)=>await Guard(Diagnostics);
        footer.Controls.Add(progress,0,0);footer.SetColumnSpan(progress,5);
        footer.Controls.Add(status,0,1);footer.Controls.Add(startupDiagnosticsLink,1,1);
        footer.Controls.Add(globalUpdateLink,2,1);footer.Controls.Add(discordCommunity,3,1);
        footer.Controls.Add(coffeeLink,4,1);
        Controls.Add(body); Controls.Add(footer); Controls.Add(header);
        body.Controls.Add(tabs); body.Controls.Add(setup);
        installButton=Button("install",InstallGame,true);
        cancelButton=new Button {AutoSize=true,MinimumSize=new Size(150,38)};Bind(cancelButton,"cancel");cancelButton.Visible=false;
        cancelButton.Click+=(_,_)=>RequestCancellation();
        BuildSetup(); BuildTabs(); BuildSettingsRecovery();
        tabs.SelectedIndexChanged += (_,_) => { if(IsHandleCreated) BeginInvoke((Action)FitMenuHeight); };
        Size sizeBeforeDrag=Size;
        ResizeBegin+=(_,_)=>sizeBeforeDrag=Size;
        ResizeEnd+=(_,_)=>{if(Size!=sizeBeforeDrag)automaticMenuHeight=false;};
        tabs.Visible=false;
        language.SelectedIndexChanged+=(_,_)=>ChangeLanguage();
        tray.DoubleClick += (_,_) => RestoreWindow();
        trayRestore.Click+=(_,_)=>RestoreWindow();trayExit.Click+=(_,_)=>Close();
        trayMenu.Items.AddRange([trayRestore,trayExit]);tray.ContextMenuStrip=trayMenu;
        Resize += (_,_) => { if (gameRunning && WindowState == FormWindowState.Minimized) { ShowInTaskbar = false; tray.Visible = true; } };
        FormClosing += async (_,e) =>
        {
            if(updateExit)return;
            if (gameRunning) { e.Cancel = true; WindowState = FormWindowState.Minimized; ShowInTaskbar = false; tray.Visible = true; }
            else if (busy) { e.Cancel = true; if (!cancelRequested&&MessageBox.Show(this,Lang.T("closeBusy"),Lang.T("app"),MessageBoxButtons.YesNo,MessageBoxIcon.Question)==DialogResult.Yes) RequestCancellation(); }
            else if(GraphicsDirty&&!closeDraftApproved)
            {
                e.Cancel=true;if(closingDraft)return;closingDraft=true;bool accepted=false;
                await Guard(async()=>accepted=await ResolveGraphicsDraft());closingDraft=false;
                if(accepted&&!IsDisposed){closeDraftApproved=true;Close();}
            }
        };
        FormClosed += (_,_) => { tray.Dispose();trayMenu.Dispose();discordSymbol.Dispose();coffeeSymbol.Dispose(); };
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
        setupHelp = Button("setupHelp", ToggleSetupHelp);
        accessibleFields[setupDetails] = "details";
        Add(stack,Label("installInfo")); Add(stack,setupNotice);
        Bind(setupNetwork,"setupNetwork");Add(stack,setupNetwork);
        Add(stack,Buttons(installButton,cancelButton,setupHelp,Button("diagnostics",Diagnostics)));
        Add(stack,setupDetails);
    }

    private Task ToggleSetupHelp()
    {
        setupDetails.Visible = !setupDetails.Visible;
        if (setupDetails.Visible)
        {
            RefreshSetupDetails();
            setupDetails.Focus();
        }
        return Task.CompletedTask;
    }

    private void RefreshSetupDetails()
    {
        setupDetails.Text = setupErrorCode is string code
            ? $"{Lang.T(code)}{Environment.NewLine}{code}{Environment.NewLine}{lastError}"
            : Lang.T("setupHelpIntro");
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
        openGraphics=Button("openGraphics",()=>{tabs.SelectedTab=graphicsPage;return Task.CompletedTask;},exclusive:false);
        Add(home,Buttons(openGraphics));
        Add(home,homeUpdateNotice);
        homeUpdateInstall=Button("updateInstall",InstallUpdate,true);
        Add(home,Buttons(Button("updates",()=>{tabs.SelectedTab=updatesPage;return Task.CompletedTask;}),homeUpdateInstall));
        var lanScroll=new Panel { Dock=DockStyle.Fill,AutoScroll=true };lanPage.Controls.Add(lanScroll);var lan = Stack(lanScroll);
        var lanTitle=Label("lanTitle");lanTitle.Font=new Font(Font.FontFamily,20,FontStyle.Bold);Add(lan,lanTitle);
        Add(lan,Label("lanIntro"));Add(lan,Field("player",player));
        lanActionBar=Buttons(Button("lanStart",()=>Play(true),true));DockActions(lanPage,lanActionBar);
        Add(lan,Label("lanSteps"));Add(lan,Label("lanTrouble"));
        Add(lan,Buttons(Button("lanPermission",Firewall)));
        Add(lan,Field("lanDiscoveryPort",lanDiscoveryPort));
        Add(lan,Label("lanDiscoveryPortInfo"));
        Add(lan,Buttons(Button("lanSavePort",()=>SaveLanDiscoveryPort((int)lanDiscoveryPort.Value)),
            Button("lanResetPort",()=>SaveLanDiscoveryPort(DefaultLanDiscoveryPort))));
        Add(lan,lanNotice);
        var internetScroll = new Panel { Dock=DockStyle.Fill, AutoScroll=true }; internetPage.Controls.Add(internetScroll);
        var net = Stack(internetScroll);
        var netTitle = Label("internetTitle"); netTitle.Font=new Font(Font.FontFamily,20,FontStyle.Bold); Add(net,netTitle);
        Add(net,Label("internetIntro"));Bind(internetUnavailable,"internetUnavailable");Add(net,internetUnavailable);
        Add(net,Label("internetNetworkSummary"));
        Add(net,internetConnection);Add(net,internetNext);
        void AddSetup(Control control){internetSetupControls.Add(control);Add(net,control);}
        BuildCrossplayControls(AddSetup);
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
        internetJoin.TextChanged+=(_,_)=>UpdateInvitationRoute();discordPublish.CheckedChanged+=(_,_)=>UpdateInternetControls();
        internetMode.SelectedIndexChanged += (_,_)=>UpdateInternetControls();
        internetEndpoint.TextChanged+=(_,_)=>UpdateInternetControls();
        BuildGraphicsPage();
        var helpScroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true }; helpPage.Controls.Add(helpScroll); var h = Stack(helpScroll);
        Add(h,Label("helpIntro"));
        var ways=Label("startGuideTitle");ways.Font=new Font(Font.FontFamily,12,FontStyle.Bold);Add(h,ways);
        Add(h,Label("startGuide"));Add(h,Label("roomGuide"));Add(h,Label("invitationGuide"));
        var maintenance=Label("maintenanceTitle");maintenance.Font=new Font(Font.FontFamily,12,FontStyle.Bold);Add(h,maintenance);
        Add(h,Label("maintenanceGuide"));
        Add(h,Buttons(Button("verify",Verify),Button("repair",ShowRepair)));
        Add(h,Buttons(Button("probe",Probe),Button("lanPermission",Firewall)));
        maintenanceCleanup=Button("cleanup",Cleanup);
        Add(h,Buttons(maintenanceCleanup,Button("diagnostics",Diagnostics))); Add(h,Label("details")); Add(h,details);
        Add(h,Label("discordTitle"));Add(h,Label("discordInfo"));
        var discordCode=new TextBox { Text=Community.DiscordCode,ReadOnly=true,Dock=DockStyle.Fill };
        Add(h,Field("discordSuggestedCode",discordCode));
        Add(h,Buttons(OpenDiscordButton("discordOpen"),
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
        RefreshSetupDetails();
        ApplyPlayModeLanguage();UpdateMaintenanceNotice();
        UpdateGraphicsPresets();ApplyEditionCapabilities();if(recoveryVisible)ShowSettingsRecovery(recoveryStatus);
        foreach (var item in accessibleFields) item.Key.AccessibleName = Lang.T(item.Value);
        Text=Lang.T("app"); tray.Text=Lang.T("app");trayRestore.Text=Lang.T("show");trayExit.Text=Lang.T("exit");
        loading=true;
        int oldWindow=window.SelectedIndex;
        window.Items.Clear();window.Items.AddRange([Lang.T("windowed"),Lang.T("borderless")]); window.SelectedIndex=Math.Max(0,oldWindow);
        int oldInternet=internetMode.SelectedIndex;internetMode.Items.Clear();internetMode.Items.AddRange([Lang.T("internetAssisted"),Lang.T("internetDirect")]);internetMode.SelectedIndex=Math.Max(0,oldInternet);
        ApplyCrossplayLanguage();
        ApplyNativeGraphicsLanguage();
        SetAntialiasingMode(AntialiasingMode);
        foreach(var combo in qualities.Values) { int old=combo.SelectedIndex;combo.Items.Clear();combo.Items.AddRange([Lang.T("low"),Lang.T("medium"),Lang.T("high")]);combo.SelectedIndex=old<0?1:old; }
        loading=false;
        if(snapshot.ValueKind==JsonValueKind.Object)LoadSnapshot(snapshot,false,preserveDirectoryConnection:true);
        TranslateDirectoryRows();
        SetGameRunning(gameRunning);
        if(!busy) status.Text=Lang.T("ready");
        UpdateInternetControls();
        RefreshUpdateControls();
        ApplyEditionCapabilities();
    }
    private async Task Initialise()
    {
        try
        {
            SetBusy(true);
            var value=await RunUiBackend("snapshot",new {}); LoadSnapshot(value);
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
            if(!await TryShowSettingsRecovery(error))
            {
                setup.Visible=true;tabs.Visible=false;installButton.Enabled=true;
                if(error is BackendError portableFailure&&portableFailure.Code=="standalone_invalid")setup.Visible=false;
                ShowError(error);
                if(error is BackendError failure&&failure.Code=="installation_invalid"){recoverState=true;Bind(installButton,"restartSetup");}
            }
        }
        finally {SetBusy(false);}
        if(!testing&&updatesSupported)_=CheckUpdates(true);
    }
    private void LoadSnapshot(JsonElement value,bool replaceFields=true,bool acceptGraphics=false,bool preserveDirectoryConnection=false)
    {
        if(GraphicsDirty&&!acceptGraphics)replaceFields=false;
        snapshot=value.Clone();loading=true;
        graphicsBackup=value.TryGetProperty("graphics_backup",out var backup)?backup.GetString():null;
        singlePlayerOnly=value.TryGetProperty("single_player_only",out var soloOnly)&&soloOnly.GetBoolean();
        maintenanceCount=value.TryGetProperty("maintenance_pending",out var pendingMaintenance)?pendingMaintenance.GetInt32():0;UpdateMaintenanceNotice();
        visualDetailAvailable=value.TryGetProperty("visual_detail_supported",out var visualAvailable)&&visualAvailable.GetBoolean();
        int legacyLodMaximum=value.TryGetProperty("lod_steps_supported",out var lodSupport)&&lodSupport.GetBoolean()?4:1;
        compatSmaaAvailable=value.TryGetProperty("compat_smaa_supported",out var compatibleAa)&&compatibleAa.GetBoolean();
        detailLevel.Maximum=value.TryGetProperty("lod_maximum",out var lodMaximum)&&lodMaximum.TryGetInt32(out int limit)
            ?Math.Clamp(limit,1,detailPercentages.Length-1):legacyLodMaximum;
        midsummerToneAvailable=value.TryGetProperty("midsummer_tone_supported",out var toneAvailable)&&toneAvailable.GetBoolean();
        updatesSupported=value.TryGetProperty("updates_supported",out var updatesAvailable)&&updatesAvailable.GetBoolean();
        lastUpdateFailure=value.TryGetProperty("update_last_error",out var updateFailure)&&updateFailure.ValueKind==JsonValueKind.String?updateFailure.GetString():null;
        screens.Clear();screens.AddRange(value.GetProperty("displays").EnumerateArray().Select(x=>x.Clone()));
        string savedRenderer=replaceFields&&value.TryGetProperty("renderer_policy",out var policy)
            ? policy.GetString()??"dxvk" : RendererMode;
        string preferredAa=AntialiasingMode;
        LoadNativeGraphics(value,replaceFields);
        if(replaceFields)
        {
            suspendedSmaa=false;
            setupNetwork.Checked=value.TryGetProperty("network_setup_preferred",out var networkSetup)&&networkSetup.GetBoolean();
            savedLanDiscoveryPort=value.TryGetProperty("lan_discovery_port",out var port)&&port.TryGetInt32(out int savedPort)
                &&savedPort is >=1024 and <=65535?savedPort:DefaultLanDiscoveryPort;
            lanDiscoveryPort.Value=savedLanDiscoveryPort;
            destination.Text=value.GetProperty("data_default").GetString(); player.Text=value.GetProperty("player_name").GetString();internetPlayer.Text=player.Text;
            simple.Checked=value.GetProperty("choice").TryGetProperty("simple_menu",out var simpleValue)&&simpleValue.GetBoolean();
            anisotropicFiltering.Checked=value.GetProperty("choice").TryGetProperty("anisotropic_filtering",out var filteringValue)&&filteringValue.GetBoolean();
            var graphicsChoice=value.GetProperty("choice");
            int savedDetail=graphicsChoice.TryGetProperty("detail_level",out var step)?step.GetInt32():
                graphicsChoice.TryGetProperty("extended_detail",out var oldDetail)&&oldDetail.GetBoolean()?1:0;
            detailLevel.Value=visualDetailAvailable?Math.Clamp(savedDetail,0,detailLevel.Maximum):0;
            linkShadowRange.Checked=visualDetailAvailable&&graphicsChoice.TryGetProperty("link_shadow_range",out var link)&&link.GetBoolean();
            detailedShadows.Checked=value.GetProperty("choice").TryGetProperty("detailed_shadows",out var shadowValue)&&shadowValue.GetBoolean();
            extendedShadowRange.Checked=value.GetProperty("choice").TryGetProperty("extended_shadow_range",out var rangeValue)&&rangeValue.GetBoolean();
            midsummerTone.Checked=midsummerToneAvailable&&value.GetProperty("choice").TryGetProperty("midsummer_tone",out var toneValue)&&toneValue.GetBoolean();
            contactShadows.Checked=midsummerToneAvailable&&value.GetProperty("choice").TryGetProperty("contact_shadows",out var contactsValue)&&contactsValue.GetBoolean();
            ultraShadows.Checked=visualDetailAvailable&&value.GetProperty("choice").TryGetProperty("ultra_shadows",out var ultraValue)&&ultraValue.GetBoolean();
            var quality=value.GetProperty("quality");foreach(var pair in qualities)pair.Value.SelectedIndex=quality.GetProperty(pair.Key).GetInt32();
            foreach(var pair in effects)pair.Value.Checked=quality.GetProperty(pair.Key).GetInt32()!=0;
            preferredAa=quality.GetProperty("Antialiasing").GetInt32()!=0?"game":"off";
            if(CanUseSmaa(savedRenderer)&&
                value.GetProperty("choice").TryGetProperty("enhanced_aa",out var aaValue)&&aaValue.GetBoolean())
                preferredAa="smaa";
        }
        internetAvailable=value.TryGetProperty("internet_available",out var available)&&available.GetBoolean();
        crossplayAvailable=value.TryGetProperty("crossplay_available",out var mixedCapability)&&mixedCapability.GetBoolean();
        discordAvailable=value.TryGetProperty("discord_available",out var discordCapability)&&discordCapability.GetBoolean();
        discordConnected=value.TryGetProperty("discord_connected",out var connected)&&connected.GetBoolean();
        directoryAvailable=value.TryGetProperty("directory_available",out var directoryCapability)&&directoryCapability.GetBoolean();
        publicHostingAvailable=value.TryGetProperty("public_directory_available",out var publicCapability)&&publicCapability.GetBoolean();
        if(!preserveDirectoryConnection)
        {
            bool knownConnection=value.TryGetProperty("directory_connected",out var directoryConnection);
            directoryConnected=knownConnection&&directoryConnection.GetBoolean();
            if(knownConnection&&!directoryConnected)
            {
                InvalidateDirectoryRefresh();
                directoryRows.Clear();directoryGames.Items.Clear();directoryLoadFailed=false;
            }
        }
        if(replaceFields)directoryPlayer.Text=player.Text;
        UpdateDirectoryControls();
        if(replaceFields)discordPublish.Checked=value.TryGetProperty("discord_publish_enabled",out var publication)&&publication.GetBoolean();
        var rendererModes=value.TryGetProperty("renderer_modes",out var modes)
            ? modes.EnumerateArray().Select(mode=>mode.GetString()!).ToArray() : new[]{"dxvk"};
        SetRendererModes(rendererModes,savedRenderer);
        SetAntialiasingMode(preferredAa);
        if(replaceFields)
            maximumFx.Checked=midsummerToneAvailable&&!singlePlayerOnly&&
                value.GetProperty("choice").TryGetProperty("maximum_fx",out var savedMaximum)&&savedMaximum.GetBoolean();
        string? oldGpu=replaceFields?value.GetProperty("choice").GetProperty("choice").GetString():(gpu.SelectedItem as Item)?.Key;
        gpu.Items.Clear();gpu.Items.Add(new Item("",Lang.T("gpuAuto")));
        var graphicsProbe=value.GetProperty("probe");
        foreach(var item in graphicsProbe.GetProperty("devices").EnumerateArray())gpu.Items.Add(new Item(item.GetString()!,item.GetString()!));
        graphicsProbeFailure=graphicsProbe.TryGetProperty("failure",out var failure)?failure.GetString():null;
        gpu.SelectedIndex=0;for(int i=1;i<gpu.Items.Count;i++)if(gpu.Items[i] is Item option&&option.Key==oldGpu)gpu.SelectedIndex=i;
        var selected=value.GetProperty("choice").TryGetProperty("display",out var found)?found:default;
        string? oldDisplay=replaceFields&&selected.ValueKind==JsonValueKind.Object?selected.GetProperty("device").GetString():(monitor.SelectedItem as Item)?.Key;
        monitor.Items.Clear();foreach(var item in screens)
        {
            string key=item.GetProperty("device").GetString()!;
            monitor.Items.Add(new Item(key,key.Split("DISPLAY").Last()+ (item.GetProperty("primary").GetBoolean()?" ("+Lang.T("primary")+")":"")+" · "+item.GetProperty("width")+" × "+item.GetProperty("height")));
        }
        if(monitor.Items.Count>0){monitor.SelectedIndex=0;for(int i=0;i<monitor.Items.Count;i++)if(monitor.Items[i] is Item option&&option.Key==oldDisplay)monitor.SelectedIndex=i;}
        if(replaceFields)
        {
            bool hasDisplay=selected.ValueKind==JsonValueKind.Object;
            keepDisplay.Checked=!hasDisplay;
            window.SelectedIndex=hasDisplay&&selected.GetProperty("mode").GetString()=="borderless"?1:0;
            if(hasDisplay&&window.SelectedIndex==0&&selected.GetProperty("device").GetString() is string device)
                windowedResolutions[device]=selected.GetProperty("width")+"x"+selected.GetProperty("height");
        }
        UpdateResolutions(allowWhileLoading:true);
        loading=false;
        UpdateGraphicsPresets();
        if(replaceFields||acceptGraphics)AcceptGraphicsDraft();else UpdateGraphicsDraft();
        ApplyEditionCapabilities();
    }
    private void ApplyEditionCapabilities()
    {
        if(modeGrid==null)return;
        var pages=new[]{homePage,lanPage,internetPage,directoryPage,graphicsPage,helpPage,updatesPage};
        foreach(var page in new[]{lanPage,internetPage,directoryPage})
            if(singlePlayerOnly&&(page!=internetPage||!crossplayAvailable))tabs.TabPages.Remove(page);
        var visiblePages=singlePlayerOnly?(crossplayAvailable?new[]{homePage,internetPage,graphicsPage,helpPage,updatesPage}:new[]{homePage,graphicsPage,helpPage,updatesPage}):pages;
        for(int i=0;i<visiblePages.Length;i++)
            if(!tabs.TabPages.Contains(visiblePages[i]))tabs.TabPages.Insert(Math.Min(i,tabs.TabPages.Count),visiblePages[i]);
        foreach(var entry in playModes)
        {
            entry.Card.Visible=!singlePlayerOnly||entry.Card.Mode==PlayMode.Solo||(crossplayAvailable&&entry.Card.Mode==PlayMode.PrivateInternet);
            modeGrid.SetColumnSpan(entry.Card,singlePlayerOnly&&!crossplayAvailable&&entry.Card.Mode==PlayMode.Solo?2:1);
            modeGrid.SetCellPosition(entry.Card,singlePlayerOnly&&crossplayAvailable&&entry.Card.Mode==PlayMode.PrivateInternet
                ?new TableLayoutPanelCellPosition(1,0):new TableLayoutPanelCellPosition(((int)entry.Card.Mode)%2,((int)entry.Card.Mode)/2));
            if(entry.Card.Mode==PlayMode.Solo)
            {
                entry.Card.Summary=Lang.T(singlePlayerOnly?"goldSoloSummary":entry.Summary);
                entry.Card.FeatureOne=Lang.T(singlePlayerOnly?"goldSoloFeatures":entry.One);
                entry.Card.RefreshDescription();
            }
            if(entry.Card.Mode==PlayMode.PrivateInternet)
            {
                bool mixed=singlePlayerOnly&&crossplayAvailable;
                entry.Card.Text=Lang.T(mixed?"crossplayTitle":"startInternetTitle");
                entry.Card.Summary=Lang.T(mixed?"crossplayGoldSummary":entry.Summary);
                entry.Card.FeatureOne=Lang.T(mixed?"crossplayPlayers":entry.One);
                entry.Card.FeatureTwo=Lang.T(mixed?"crossplayHostFeature":entry.Two);
                entry.Card.RefreshDescription();
            }
        }
        foreach(var item in labels.Where(x=>x.Value=="modeConsentInfo"))item.Key.Visible=!singlePlayerOnly;
        foreach(var item in labels.Where(x=>x.Value is "roomGuide" or "invitationGuide"))item.Key.Visible=!singlePlayerOnly;
        foreach(var item in labels.Where(x=>x.Value=="startGuide"))item.Key.Text=Lang.T(singlePlayerOnly?IdleHomeInfo:"startGuide");
        openGraphics.Visible=true;
        if(!gameRunning)homeInfo.Text=Lang.T(IdleHomeInfo);
        UpdateCrossplayControls();
    }
    private string IdleHomeInfo=>singlePlayerOnly?(crossplayAvailable?"goldMixedIntro":"goldIntro"):"playIntro";
    private void SetGameRunning(bool running)
    {
        gameRunning=running;
        homeTitle.Text=Lang.T(running?"running":"playTitle");
        homeInfo.Text=Lang.T(running?"runningInfo":IdleHomeInfo);
    }
    private void ShowMain(){setup.Visible=false;tabs.Visible=true;startupDiagnosticsLink.Visible=false;forceSetup=false;SetGameRunning(false);status.Text=Lang.T("ready");FitMenuHeight();}
    private async Task ShowSetup()
    {
        if(!await ResolveGraphicsDraft())return;
        tabs.Visible=false;setup.Visible=true;
        Bind(installButton,snapshot.GetProperty("installed").GetBoolean()?"update":"install");
        source.Items.Clear();
        Bind(setupNotice,"detecting");var found=await RunUiBackend("detect",new {});
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
        setupErrorCode=null;RefreshSetupDetails();
        var result=await RunUiBackend("install",new {source=item.Key,container=destination.Text,prepare_network=setupNetwork.Checked});
        LoadSnapshot(result);if(!testing)Shortcuts.Create();ShowMain();
        if(result.TryGetProperty("network_setup_failures",out var failures)&&failures.GetArrayLength()>0)
        {
            status.Text=Lang.T("networkSetupPartial");
            if(!testing)MessageBox.Show(this,status.Text+Environment.NewLine+Environment.NewLine+
                string.Join(Environment.NewLine,failures.EnumerateArray().Select(item=>Lang.T(item.GetString()!))),
                Lang.T("setupNetworkTitle"),MessageBoxButtons.OK,MessageBoxIcon.Information);
        }
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
        remoteUpdateVersion=remoteUpdateAvailable?Program.DisplayVersion(value.GetProperty("version").GetString()??""):"";
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
        rebuildingResolutions=true;
        bool unsupported=false;
        try
        {
            resolution.Items.Clear();
            if(monitor.SelectedIndex>=0&&monitor.SelectedIndex<screens.Count&&monitor.SelectedItem is Item display)
            {
                var screen=screens[monitor.SelectedIndex];
                int width=screen.GetProperty("width").GetInt32(),height=screen.GetProperty("height").GetInt32();
                bool borderless=window.SelectedIndex==1;
                string? previous=borderless?null:windowedResolutions.GetValueOrDefault(display.Key);
                int preferred=-1,fallback=0,largestSafe=0;
                foreach(var size in screen.GetProperty("resolutions").EnumerateArray())
                {
                    int w=size[0].GetInt32(),h=size[1].GetInt32();
                    if(borderless&&(w!=width||h!=height))continue;
                    string key=w+"x"+h;
                    int index=resolution.Items.Add(new Item(key,w+" × "+h));
                    if(key==previous)preferred=index;
                    if(w<=1920&&h<=1080&&w*h>largestSafe){fallback=index;largestSafe=w*h;}
                }
                if(resolution.Items.Count>0)resolution.SelectedIndex=preferred>=0?preferred:fallback;
                unsupported=borderless&&(width>3840||height>3840||(long)width*height>3840L*2160||resolution.Items.Count==0);
            }
        }
        finally{rebuildingResolutions=false;}
        if(unsupported)
        {
            status.Text=Lang.T("unsupportedSize");window.SelectedIndex=0;
            if(loading)UpdateResolutions(allowWhileLoading:true);
            return;
        }
        DisplayEnabled();
    }
    private void ResolutionSelected()
    {
        if(loading||rebuildingResolutions)return;
        if(window.SelectedIndex==0&&monitor.SelectedItem is Item display&&resolution.SelectedItem is Item size)
            windowedResolutions[display.Key]=size.Key;
        DisplayEnabled();
    }
    private void DisplayEnabled()
    {
        UpdateGraphicsPresets();
        UpdateNativeGraphics();
        bool manual=!keepDisplay.Checked&&!busy;
        monitor.Enabled=manual&&monitor.Items.Count>0;
        window.Enabled=manual&&monitor.SelectedIndex>=0;
        resolution.Enabled=manual&&window.SelectedIndex==0&&resolution.Items.Count>0;
        renderer.Enabled=!busy&&renderer.Items.Count>1;
        bool modern=RendererMode=="dxvk";
        bool smaaAvailable=SmaaAvailable;
        if(!loading&&antialiasing.Items.Count!=(smaaAvailable?3:2))
        {
            string mode=smaaAvailable&&suspendedSmaa?"smaa":AntialiasingMode;
            suspendedSmaa=!smaaAvailable&&mode=="smaa";
            SetAntialiasingMode(mode);
        }
        antialiasing.Enabled=!busy;
        graphicsGpuField.Visible=modern;
        gpu.Enabled=!busy&&modern&&gpu.Items.Count>1;
        detailLevelField.Visible=detailLevelValue.Visible=linkShadowRange.Visible=visualDetailAvailable;
        detailLevel.Enabled=linkShadowRange.Enabled=!busy&&visualDetailAvailable;
        detailLevelValue.Text=detailLevel.Value==0?Lang.T("antialiasingOff"):$"+{detailPercentages[detailLevel.Value]} %";
        foreach(var item in additionalGraphics)
        {
            bool available=item.Key==anisotropicFiltering?modern:
                item.Key==maximumFx?MaximumGraphicsAvailable:
                item.Key==midsummerTone||item.Key==contactShadows?AdvancedReshadeAvailable:
                item.Key==extendedShadowRange?visualDetailAvailable&&!linkShadowRange.Checked:visualDetailAvailable;
            item.Key.Visible=item.Value.Visible=available;
            item.Key.Enabled=!busy&&available;
        }
    }
    private void ApplyQualityPreset(bool maximumObjects)
    {
        suspendedSmaa=false;
        foreach(var pair in qualities)pair.Value.SelectedIndex=pair.Key=="ObjectQuality"&&!maximumObjects?1:2;
        foreach(var pair in effects)pair.Value.Checked=pair.Key!="DepthOfField";
        SetAntialiasingMode(SmaaAvailable?"smaa":"game");
        if(RendererMode=="dxvk")anisotropicFiltering.Checked=true;
    }
    private void ApplyRecommendedGraphics()
    {
        DisplayEnabled();
        ApplyQualityPreset(true);
        detailLevel.Value=visualDetailAvailable?1:0;
        linkShadowRange.Checked=false;
        detailedShadows.Checked=extendedShadowRange.Checked=visualDetailAvailable;
        ultraShadows.Checked=false;
        maximumFx.Checked=false;
        midsummerTone.Checked=contactShadows.Checked=false;
    }
    private void ApplyMaximumGraphics()
    {
        ApplyRecommendedGraphics();
        ultraShadows.Checked=visualDetailAvailable;
        midsummerTone.Checked=midsummerToneAvailable;
        maximumFx.Checked=MaximumGraphicsAvailable;
    }
    private object GraphicsRequest()
    {
        if(GraphicsDisplayIssue is string issue)throw new BackendError(issue,"");
        object? output=null;
        if(!keepDisplay.Checked)
        {
            if(monitor.SelectedItem is not Item item||resolution.SelectedItem is not Item size)throw new BackendError("request_invalid","");
            string[] dimensions=size.Key.Split('x');output=new {device=item.Key,width=int.Parse(dimensions[0]),height=int.Parse(dimensions[1]),mode=window.SelectedIndex==1?"borderless":"windowed"};
        }
        var quality=qualities.ToDictionary(x=>x.Key,x=>x.Value.SelectedIndex);foreach(var pair in effects)quality[pair.Key]=pair.Value.Checked?1:0;
        quality["Antialiasing"]=AntialiasingMode=="game"?1:0;
        string? card=(gpu.SelectedItem as Item)?.Key;
        if(string.IsNullOrEmpty(card))card=null;
        return new {renderer_policy=RendererMode,choice=new {format="s7-graphics-choice-v1",choice=card,simple_menu=simple.Checked,display=output,
            anisotropic_filtering=anisotropicFiltering.Checked,extended_detail=detailLevel.Value>0,
            detail_level=detailLevel.Value,link_shadow_range=linkShadowRange.Checked,
            detailed_shadows=detailedShadows.Checked,extended_shadow_range=extendedShadowRange.Checked,midsummer_tone=midsummerTone.Checked,
            enhanced_aa=AntialiasingMode=="smaa",contact_shadows=contactShadows.Checked,ultra_shadows=ultraShadows.Checked,maximum_fx=maximumFx.Checked,
            native_graphics=NativeGraphicsValues()},quality,
            player_name=snapshot.GetProperty("player_name").GetString()};
    }
    private static string ValidName(string name)
    {
        if(name.Trim()!=name||Encoding.UTF8.GetByteCount(name) is <1 or >24||name.EnumerateRunes().Any(r=>
            r.Value is 0x2028 or 0x2029||Rune.GetUnicodeCategory(r) is UnicodeCategory.Control or UnicodeCategory.Format or UnicodeCategory.PrivateUse or UnicodeCategory.OtherNotAssigned or UnicodeCategory.Surrogate))throw new BackendError("nameInvalid","");
        return name;
    }
    private async Task SaveGraphics()
    {
        ApplySettingsResult(await RunUiBackend("save",GraphicsRequest()));
    }
    private async Task RestoreGraphics()
    {
        ApplySettingsResult(await RunUiBackend("restore-graphics",new {}));
        if(status.Text!=Lang.T("settings_saved_refresh_failed"))status.Text=Lang.T("graphics_restored");
    }
    private async Task Probe()
    {
        await RunUiBackend("probe",new {});
        LoadSnapshot(await RunUiBackend("snapshot",new {}),false,preserveDirectoryConnection:true);
        status.Text=Lang.T(graphicsProbeFailure switch {
            "unsupported"=>"graphicsProbeUnconfirmed",
            "probe_failed"=>"graphicsProbeFailed",
            _=>"graphicsProbeComplete"});
    }
    private async Task Verify(){await Backend.Run("verify",new {},Receive);status.Text=Lang.T("verified");}
    private async Task ShowRepair(){forceSetup=true;await ShowSetup();}
    private async Task Cleanup()
    {
        if(MessageBox.Show(this,Lang.T("cleanupConfirm"),Lang.T("app"),MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
        await Backend.Run("cleanup",new {},Receive);LoadSnapshot(await Backend.Run("snapshot",new {},Receive));status.Text=Lang.T("cleanupDone");
    }
    private async Task Diagnostics()
    {
        var value=await CollectDiagnostics();
        if(testing)return;
        using var dialog=new SaveFileDialog { FileName="S7-Diagnose.json",Filter="JSON (*.json)|*.json" };
        if(dialog.ShowDialog(this)==DialogResult.OK){await File.WriteAllTextAsync(dialog.FileName,JsonSerializer.Serialize(value,new JsonSerializerOptions {WriteIndented=true}));status.Text=Lang.T("reportSaved");}
    }
    private async Task<System.Text.Json.Nodes.JsonObject> CollectDiagnostics()
    {
        JsonElement backend=default;
        Exception? failure=null;
        try{backend=await RunUiBackend("diagnostics",new {});}
        catch(Exception error){failure=error;}
        return LauncherDiagnostics.Export(Program.AppRoot,Program.ReleaseVersion,backend,failure);
    }
    private async Task Firewall()
    {
        await Backend.Run("firewall",new {},Receive);
        status.Text=Lang.T("permissionDone");
    }
    private async Task SaveLanDiscoveryPort(int port)
    {
        var result=await RunUiBackend("lan-port",new {port});
        savedLanDiscoveryPort=result.GetProperty("lan_discovery_port").GetInt32();
        lanDiscoveryPort.Value=savedLanDiscoveryPort;
        status.Text=Lang.T("lanPortSaved");
    }
    private async Task Play(bool lan)
    {
        string? name=lan?ValidName(player.Text):null;
        if(!await ResolveGraphicsDraft())return;
        JsonElement result;
        if(lan)
        {
            if(lanDiscoveryPort.Value!=savedLanDiscoveryPort)await SaveLanDiscoveryPort((int)lanDiscoveryPort.Value);
            result=await Backend.Run("lan-play",new {player_name=name},Receive);
        }
        else result=await Backend.Run("play",new {},Receive);
        QueueDirectorySwitch(result);
        SetGameRunning(false);RestoreWindow();LoadSnapshot(await Backend.Run("snapshot",new {},Receive));status.Text=Lang.T("ready");
    }
    private bool? activeInternetDirect;
    private Func<InternetChannel>? testInternetChannelFactory;
    private void UpdateInternetControls()
    {
        bool available=MixedInternet?crossplayAvailable:internetAvailable;
        bool direct=internetMode.SelectedIndex==1;
        bool endpointRequired=direct&&string.IsNullOrWhiteSpace(internetEndpoint.Text);
        internetPolicy.Text=Lang.T(direct?"internetDirectInfo":"internetAssistedInfo");
        bool fromInvite=internetOperation?!internetCreating&&activeInternetDirect==null:internetGuestChoice.Checked;
        bool connectionDirect=internetOperation?activeInternetDirect??direct:direct;
        internetConnection.Text=Lang.T(!internetOperation&&internetHostChoice.Checked&&endpointRequired?"internetEndpointRequired":
            fromInvite?"internetConnectionFromInvite":
            connectionDirect?"internetConnectionDirect":"internetConnectionAuto");
        internetUnavailable.Visible=!available;
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
        if(internetCreate!=null)internetCreate.Enabled=!busy&&available&&!endpointRequired;
        if(internetJoinButton!=null)internetJoinButton.Enabled=!busy&&available&&Community.TryInvitationCode(internetJoin.Text,out _);
        if(internetPaste!=null)internetPaste.Enabled=!busy&&available;
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
        if(discordPublish.Checked&&(!discordAvailable||!discordConnected))discordPublish.Checked=false;
        discordPublish.Enabled=!busy&&discordAvailable&&discordConnected;discordPublish.Visible=discordAvailable;
        if(discordConnect!=null)discordConnect.Enabled=!busy&&discordAvailable;
        if(discordDisconnect!=null)discordDisconnect.Enabled=!busy&&discordAvailable&&discordConnected;
        if(discordOptionsPanel!=null)discordOptionsPanel.Visible=discordOptions.Checked;
        discordNotice.Text=Lang.T(!discordAvailable?"discordUnavailableInfo":!discordConnected?"discordNotConnected":"directoryPublisherReady");
        UpdateCrossplayControls();
        UpdateDirectoryControls();
    }
    private void UpdateInvitationRoute()
    {
        if(!internetOperation&&crossplayAvailable&&!singlePlayerOnly&&Community.TryInvitationCode(internetJoin.Text,out var code))
        {
            if(code.StartsWith("weplayx1.",StringComparison.Ordinal))internetSessionKind.SelectedIndex=1;
            else if(code.StartsWith("weplay1.",StringComparison.Ordinal))internetSessionKind.SelectedIndex=0;
        }
        UpdateInternetControls();
    }
    private void PasteInternetInvitation()
    {
        // Read the clipboard only after the explicit paste action. Never log it.
        try
        {
            string code=Community.InvitationCode(Clipboard.GetText());
            internetJoin.Text=code;
            UpdateInvitationRoute();
        }
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
    private void BeginInternetOperation(bool create,bool publicRoom,bool direct)
    {
        publicRoomOperation=publicRoom;
        activeInternetDirect=create?direct:null;
        internetCreating=create;internetStage="connecting";internetOperation=true;
        internetInvite.Clear();SetBusy(true);
        tabs.SelectedTab=publicRoomOperation?directoryPage:internetPage;
    }
    private void EndInternetOperation()
    {
        CloseGameInvitation();internetInvite.Clear();
        internetOperation=false;publicRoomOperation=false;activeInternetDirect=null;
        UpdateInternetControls();
    }
    private async Task PlayInternet(bool create,bool publish=false,bool publicLobby=false)
    {
        if(!await ResolveGraphicsDraft())return;
        bool mixed=MixedInternet&&!publish&&!publicLobby;
        if(mixed&&(create!=singlePlayerOnly||create&&crossplayHistory.SelectedIndex<0))throw new BackendError("crossplay_configuration_invalid","");
        string name=ValidName(publicLobby?directoryPlayer.Text:internetPlayer.Text);
        if(mixed&&name.Any(c=>c<32||c>126))throw new BackendError("crossplay_name_invalid","");
        string? invitation=create?null:Community.InvitationCode(internetJoin.Text);
        if(!create&&(string.IsNullOrWhiteSpace(invitation)||invitation.Length>2048))throw new BackendError("internet_invitation_invalid","");
        bool direct=!publicLobby&&internetMode.SelectedIndex==1;
        string? endpoint=direct?internetEndpoint.Text:null;
        if(create&&direct&&string.IsNullOrWhiteSpace(endpoint))throw new BackendError("internetEndpointRequired","");
        if(!ConfirmInternetConnection(publicLobby||publish))return;
        using var channel=testing&&testInternetChannelFactory!=null?testInternetChannelFactory():new InternetChannel();
        BeginInternetOperation(create,publicLobby||publish,direct);
        Task privateTask=channel.Run(invitation,code=>
        {
            void Accept(){if(internetOperation){internetInvite.Text=code;internetStage="ready";UpdateInternetControls();}}
            if(InvokeRequired)BeginInvoke(new Action(Accept));else Accept();
        });
        try
        {
            JsonElement result;
            if(mixed&&create)
                result=await RunUiBackend("crossplay-create",new {player_name=name,channel=channel.Name,history_edition=crossplayHistory.SelectedIndex==0?2:3,
                    mode=direct?"direct":"assisted",direct_endpoint=endpoint,internet_consent=1});
            else if(mixed)
                result=await RunUiBackend("crossplay-join",new {player_name=name,channel=channel.Name,internet_consent=1});
            else if(create)
            {
                var request=new Dictionary<string,object?> { ["player_name"]=name,["channel"]=channel.Name,["mode"]=direct?"direct":"assisted",
                    ["direct_endpoint"]=endpoint,["publish_discord"]=publish,["internet_consent"]=1 };
                if(publicHostingAvailable)request["public_lobby"]=publicLobby;
                result=await RunUiBackend("internet-create",request);
            }
            else result=await RunUiBackend("internet-join",new {player_name=name,channel=channel.Name,publish_discord=false,internet_consent=1});
            QueueDirectorySwitch(result);
            channel.Dispose();await privateTask;
            if(!create)internetJoin.Clear();
            SetGameRunning(false);RestoreWindow();LoadSnapshot(await RunUiBackend("snapshot",new {}));status.Text=Lang.T("ready");
        }
        finally
        {
            channel.Dispose();
            try{await privateTask;}catch(Exception){/* No private bytes enter exception details. */}
            EndInternetOperation();
        }
    }
    private void Receive(JsonElement value)
    {
        if(InvokeRequired){BeginInvoke(new Action(()=>Receive(value)));return;}
        string? kind=value.GetProperty("kind").GetString();
        if(kind=="directory_switch")
        {
            // The awaited result owns the switch. Progress notifications may
            // arrive later and must not change a subsequent operation's status.
            return;
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
            SetGameRunning(true);status.Text=Lang.T("running");progress.Visible=false;
            if(internetOperation)internetStage="playing";UpdateInternetControls();
        }
        else if(kind=="game_finished")
        {
            CloseGameInvitation();
            SetGameRunning(false);
            if(internetOperation)internetStage="hosting";status.Text=Lang.T("ready");UpdateInternetControls();
        }
        else if(kind=="invitation_requested")OpenGameInvitation(value);
        else if(kind=="internet_policy")
        {
            if(internetOperation)activeInternetDirect=value.GetProperty("mode").GetString()=="direct";
            UpdateInternetControls();
        }
        else if(kind=="warning")
        {
            string code=value.GetProperty("code").GetString()!;
            if(!cancelRequested)status.Text=Lang.T(code);
            if(!testing)
            {
                tray.BalloonTipTitle=Lang.T("app");tray.BalloonTipText=Lang.T(code);tray.Visible=true;tray.ShowBalloonTip(10000);
            }
        }
    }
    private void SetBusy(bool value)
    {
        if(value)InvalidateDirectoryRefresh();
        busy=value;tabs.Enabled=!value||internetOperation;source.Enabled=destination.Enabled=installButton.Enabled=setupNetwork.Enabled=!value;language.Enabled=!value;
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
        UpdateRecoveryControls();
        ApplyEditionCapabilities();
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
            pendingDirectoryGame=null;
            if(showErrors)
            {
                SetGameRunning(false);RestoreWindow();
                if(error is BackendError {Code:"graphics_start_reverted"})
                {
                    try{LoadSnapshot(await RunUiBackend("snapshot",new {}),acceptGraphics:true);}
                    catch(BackendError){} // Keep the error that explains the completed rollback.
                }
                if(!await TryShowSettingsRecovery(error))ShowError(error);
            }
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
        if(!testing)LauncherDiagnostics.RecordUiError(Program.AppRoot,error);
        startupDiagnosticsLink.Visible=!tabs.Visible&&!setup.Visible;
        if(setup.Visible)
        {
            setupErrorCode=code;
            RefreshSetupDetails();
            setupDetails.Visible=true;
            if(setupDetails.Parent?.Parent is Panel scroll)scroll.ScrollControlIntoView(setupDetails);
        }
        visibleErrorCode=code;status.Text=Lang.T(code);
        if(!testing)MessageBox.Show(this,Lang.T(code)+Environment.NewLine+Environment.NewLine+
            string.Format(Lang.T("diagnosticErrorHint"),code),Lang.T("errorTitle"),MessageBoxButtons.OK,MessageBoxIcon.Warning);
    }
    private void RestoreWindow()
    {
        ShowInTaskbar=true;
        if(WindowState==FormWindowState.Minimized)WindowState=FormWindowState.Normal;
        tray.Visible=false;Show();
    }

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
            {"installed":true,"updates_supported":true,"displays":[{"device":"\\\\.\\DISPLAY1","adapter":"Fixture GPU","primary":true,"x":0,"y":0,"width":1920,"height":1080,"resolutions":[[1280,720],[1920,1080]]}],"data_default":"C:\\Fixture\\Instances","source":"C:\\Fixture\\Original","renderer_policy":"dxvk","renderer_modes":["dxvk","async","gplasync","native"],"probe":{"devices":["Fixture GPU"],"failure":null},"choice":{"format":"s7-graphics-choice-v1","choice":"Fixture GPU","simple_menu":true,"display":{"device":"\\\\.\\DISPLAY1","width":1920,"height":1080,"mode":"borderless"}},"quality":{"ShaderQuality":1,"TextureQuality":1,"ObjectQuality":1,"DistanceCulling":1,"ShadowQuality":1,"WaterQuality":1,"ClutterQuality":1,"Shadows":1,"SSAO":0,"DepthOfField":0,"SunShafts":0,"Parallax":0,"Antialiasing":1},"player_name":"Tester","renderer":"dxvk","current":"C:\\Fixture\\Game","update_available":false}
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
                if(form.releaseVersion.Text!="rc4.13"||Program.ReleaseVersion!="rc4.13")throw new Exception("Release version label must be exactly rc4.13");
                if(Program.DisplayVersion("2.0.0-rc4.0+fixture")!="rc4.00")throw new Exception("Technical update version leaked into display");
                form.LoadSnapshot(json.RootElement,acceptGraphics:true);form.ShowMain();form.Show();Application.DoEvents();
                form.tabs.SelectedTab=form.lanPage;
                if(form.lanDiscoveryPort.Value!=DefaultLanDiscoveryPort)throw new Exception("LAN discovery must default to 9112");
                int requestedPort=0;
                form.testUiBackend=(action,data)=>{
                    if(action!="lan-port")throw new Exception("Unexpected LAN settings action");
                    requestedPort=JsonSerializer.SerializeToElement(data).GetProperty("port").GetInt32();
                    return Task.FromResult(JsonSerializer.SerializeToElement(new {lan_discovery_port=requestedPort}));
                };
                try
                {
                    form.lanDiscoveryPort.Value=19112;
                    ((Button)form.labels.Single(item=>item.Value=="lanSavePort").Key).PerformClick();
                    if(requestedPort!=19112||form.savedLanDiscoveryPort!=19112)throw new Exception("LAN port was not saved");
                    ((Button)form.labels.Single(item=>item.Value=="lanResetPort").Key).PerformClick();
                    if(requestedPort!=DefaultLanDiscoveryPort||form.lanDiscoveryPort.Value!=DefaultLanDiscoveryPort)
                        throw new Exception("LAN port was not restored");
                }
                finally{form.testUiBackend=null;form.tabs.SelectedTab=form.homePage;}
                form.TestStandaloneEdition(json.RootElement,folder,language);
                form.TestCrossplayEdition(json.RootElement,folder,language);
                form.TestLauncherResilience(json.RootElement);
                TestLauncherAudit(json.RootElement);
                TestLauncherInteractions(json.RootElement);
                TestLauncherPresentation(json.RootElement);
                TestLauncherRooms(json.RootElement);
                TestLauncherDirectoryState(json.RootElement);
                TestLauncherLifecycle(json.RootElement);
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
                if(form.detailLevel.Value!=0||form.detailedShadows.Checked||form.extendedShadowRange.Checked)throw new Exception("Legacy preferences force visual detail");
                if(form.detailLevel.Enabled||form.detailedShadows.Enabled||form.extendedShadowRange.Enabled)throw new Exception("Unsupported adapter exposes visual controls");
                form.visualDetailAvailable=true;form.DisplayEnabled();
                form.detailLevel.Value=1;form.detailedShadows.Checked=true;form.extendedShadowRange.Checked=true;
                using(var detailed=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest())))
                {
                    var detailChoice=detailed.RootElement.GetProperty("choice");
                    if(!detailChoice.GetProperty("extended_detail").GetBoolean()||!detailChoice.GetProperty("detailed_shadows").GetBoolean()||!detailChoice.GetProperty("extended_shadow_range").GetBoolean())
                        throw new Exception("Visual detail choices lost in request");
                }
                form.midsummerToneAvailable=true;form.DisplayEnabled();form.midsummerTone.Checked=true;
                form.SetAntialiasingMode("smaa");form.contactShadows.Checked=true;form.ultraShadows.Checked=true;form.maximumFx.Checked=true;
                using(var tone=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest())))
                    if(!form.midsummerTone.Enabled||!form.antialiasing.Enabled||!form.contactShadows.Enabled
                        ||!tone.RootElement.GetProperty("choice").GetProperty("midsummer_tone").GetBoolean()
                        ||!tone.RootElement.GetProperty("choice").GetProperty("enhanced_aa").GetBoolean()
                        ||!tone.RootElement.GetProperty("choice").GetProperty("contact_shadows").GetBoolean()
                        ||!tone.RootElement.GetProperty("choice").GetProperty("ultra_shadows").GetBoolean()
                        ||!tone.RootElement.GetProperty("choice").GetProperty("maximum_fx").GetBoolean())throw new Exception("Visual quality choice lost");
                form.midsummerTone.Checked=form.contactShadows.Checked=form.ultraShadows.Checked=form.maximumFx.Checked=false;
                form.SetAntialiasingMode("off");
                form.detailLevel.Value=0;form.detailedShadows.Checked=false;form.extendedShadowRange.Checked=false;
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
                        ||recommended.RootElement.GetProperty("quality").GetProperty("Antialiasing").GetInt32()!=0
                        ||choice.GetProperty("ultra_shadows").GetBoolean()||choice.GetProperty("maximum_fx").GetBoolean()
                        ||choice.GetProperty("midsummer_tone").GetBoolean()||choice.GetProperty("contact_shadows").GetBoolean()
                        ||!new[]{"extended_detail","detailed_shadows","extended_shadow_range","enhanced_aa"}.All(name=>choice.GetProperty(name).GetBoolean()))
                        throw new Exception("Recommended graphics profile failed");
                }
                form.tabs.SelectedTab=form.graphicsPage;Application.DoEvents();
                var beforeMaximum=JsonSerializer.SerializeToElement(form.GraphicsRequest()).GetProperty("choice");
                form.graphicsMaximum.PerformClick();
                var maximum=JsonSerializer.SerializeToElement(form.GraphicsRequest());
                var maximumChoice=maximum.GetProperty("choice");
                var maximumQuality=maximum.GetProperty("quality");
                if(maximum.GetProperty("renderer_policy").GetString()!="dxvk"
                    ||maximumQuality.GetProperty("Antialiasing").GetInt32()!=0
                    ||maximumQuality.GetProperty("DepthOfField").GetInt32()!=0
                    ||form.qualities.Keys.Any(name=>maximumQuality.GetProperty(name).GetInt32()!=2)
                    ||!new[]{"Shadows","SSAO","SunShafts","Parallax"}.All(name=>maximumQuality.GetProperty(name).GetInt32()==1)
                    ||!new[]{"anisotropic_filtering","extended_detail","detailed_shadows","extended_shadow_range",
                        "enhanced_aa","midsummer_tone","ultra_shadows","maximum_fx"}.All(name=>maximumChoice.GetProperty(name).GetBoolean())
                    ||maximumChoice.GetProperty("contact_shadows").GetBoolean()
                    ||!new[]{"choice","simple_menu","display"}.All(name=>maximumChoice.GetProperty(name).GetRawText()==beforeMaximum.GetProperty(name).GetRawText()))
                    throw new Exception("Maximum preset did not keep the sharp foliage settings and local device choices");
                var dualAa=System.Text.Json.Nodes.JsonNode.Parse(json.RootElement.GetRawText())!.AsObject();
                dualAa["midsummer_tone_supported"]=true;
                dualAa["choice"]!["enhanced_aa"]=true;
                form.LoadSnapshot(JsonSerializer.SerializeToElement(dualAa),acceptGraphics:true);
                var legacyAa=JsonSerializer.SerializeToElement(form.GraphicsRequest());
                if(form.AntialiasingMode!="smaa"||legacyAa.GetProperty("quality").GetProperty("Antialiasing").GetInt32()!=0)
                    throw new Exception("A legacy double-AA configuration did not resolve to SMAA");
                foreach(var mode in new[]{"game","off","smaa"})
                {
                    form.SetAntialiasingMode(mode);
                    var aaRequest=JsonSerializer.SerializeToElement(form.GraphicsRequest());
                    if(aaRequest.GetProperty("quality").GetProperty("Antialiasing").GetInt32()!=(mode=="game"?1:0)
                        ||aaRequest.GetProperty("choice").GetProperty("enhanced_aa").GetBoolean()!=(mode=="smaa"))
                        throw new Exception("Antialiasing is not exclusive");
                }
                form.graphicsEffectDetails.Checked=true;
                for(int i=1;i<form.renderer.Items.Count;i++)
                {
                    form.renderer.SelectedIndex=i;
                    form.ApplyRecommendedGraphics();
                    using var alternative=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest()));
                    if(!form.GraphicsDirty||form.graphicsGpuField.Visible||form.antialiasing.Items.Count!=2
                        ||form.anisotropicFiltering.Visible||form.midsummerTone.Visible||form.contactShadows.Visible||form.graphicsMaximum.Visible
                        ||form.AntialiasingMode!="game"
                        ||alternative.RootElement.GetProperty("renderer_policy").GetString()!=new[]{"async","gplasync","native"}[i-1])
                        throw new Exception("Graphics override is unavailable or exposes a DXVK 3 setting");
                }
                form.DiscardGraphicsDraft();
                if(form.RendererMode!="dxvk"||form.AntialiasingMode!="smaa"||form.GraphicsDirty)
                    throw new Exception("Renderer draft was not discarded");
                dualAa["visual_detail_supported"]=true;
                dualAa["lod_steps_supported"]=true;
                dualAa["compat_smaa_supported"]=true;
                dualAa["choice"]!["extended_detail"]=true;
                form.LoadSnapshot(JsonSerializer.SerializeToElement(dualAa),acceptGraphics:true);
                if(form.detailLevel.Maximum!=4||form.detailLevel.Value!=1)
                    throw new Exception("The old LOD choice did not become the +50% step");
                dualAa["lod_maximum"]=5;
                form.LoadSnapshot(JsonSerializer.SerializeToElement(dualAa),acceptGraphics:true);
                if(form.detailLevel.Maximum!=5)throw new Exception("The adapter's extended LOD step is unavailable");
                foreach(int index in new[]{1,2})
                {
                    form.renderer.SelectedIndex=index;
                    form.ApplyRecommendedGraphics();
                    form.detailLevel.Value=5;
                    form.linkShadowRange.Checked=true;
                    var compat=JsonSerializer.SerializeToElement(form.GraphicsRequest());
                    var compatChoice=compat.GetProperty("choice");
                    if(form.graphicsMaximum.Visible||form.midsummerTone.Visible||form.extendedShadowRange.Visible
                        ||form.AntialiasingMode!="smaa"||compat.GetProperty("quality").GetProperty("Antialiasing").GetInt32()!=0
                        ||compatChoice.GetProperty("detail_level").GetInt32()!=5||!compatChoice.GetProperty("link_shadow_range").GetBoolean()
                        ||form.detailLevelValue.Text!="+400 %")
                        throw new Exception("Compatibility SMAA or linked LOD range is unavailable");
                }
                form.LoadSnapshot(json.RootElement,acceptGraphics:true);
                form.visualDetailAvailable=true;form.DisplayEnabled();
                if(!form.detailLevel.Enabled||!form.detailedShadows.Enabled||!form.extendedShadowRange.Enabled||!form.ultraShadows.Enabled)throw new Exception("DXVK visual detail controls unavailable");
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
                form.tabs.SelectedTab=form.graphicsPage;form.ApplyQualityPreset(false);
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
                var discordButtons = Descendants(form).OfType<ButtonBase>().Where(button => ReferenceEquals(button.Image, form.discordSymbol))
                    .Select(button => form.labels[button]).OrderBy(key => key, StringComparer.Ordinal).ToArray();
                var discordMarks = Descendants(form).OfType<PictureBox>().Where(mark => ReferenceEquals(mark.Image, form.discordSymbol)).ToArray();
                if(discordMarks.Length != 1 || discordMarks[0].Parent != form.discordLink.Parent
                    ||!discordButtons.SequenceEqual(new[]{"discordFindPlayers","discordOpen"})
                    ||Descendants(form).OfType<Label>().Any(label => ReferenceEquals(label.Image,form.discordSymbol))
                    ||form.labels.Any(item => (item.Value is "startGuide" or "roomGuide" or "invitationGuide") && item.Key.Padding.Left != 0))
                    throw new Exception("Discord symbols must appear only on controls that open Discord");
                form.tabs.SelectedTab=form.helpPage;
                var helpScroll=(Panel)form.helpPage.Controls[0];
                helpScroll.AutoScrollPosition=Point.Empty;
                Application.DoEvents();Capture("help-start-guide");
                helpScroll.AutoScrollPosition=new Point(0,helpScroll.DisplayRectangle.Height);Application.DoEvents();
                using(var supportImage=new Bitmap(form.Width,form.Height))
                {form.DrawToBitmap(supportImage,new Rectangle(0,0,form.Width,form.Height));supportImage.Save(Path.Combine(folder,language+"-help-community.png"));}
                form.updatesSupported=true;
                using(var offered=JsonDocument.Parse("""{"available":true,"version":"2.0.0-rc4.12","sequence":41,"notes":"UI fixture","reason":"available"}"""))form.ShowUpdateResult(offered.RootElement);
                form.tabs.SelectedTab=form.homePage;Application.DoEvents();
                if(!form.homeUpdateInstall.Visible||!form.homeUpdateInstall.Enabled||!form.homeUpdateNotice.Text.Contains("rc4.12")||form.homeUpdateNotice.Text.Contains("2.0.0")||form.globalUpdateLink.Text.Contains("2.0.0"))throw new Exception("Update notice must display rc4.12 without a technical prefix");
                form.SetBusy(true);if(form.homeUpdateInstall.Enabled)throw new Exception("Update enabled during another operation");form.SetBusy(false);
                using(var updateImage=new Bitmap(form.Width,form.Height))
                {form.DrawToBitmap(updateImage,new Rectangle(0,0,form.Width,form.Height));updateImage.Save(Path.Combine(folder,language+"-update-found.png"));}
                form.tabs.Visible=false;form.setup.Visible=true;form.source.Items.Add(new Item("C:\\Fixture\\Original","C:\\Fixture\\Original"));form.source.SelectedIndex=0;form.setupNotice.Text=Lang.T("found");Application.DoEvents();
                if(!form.setupNetwork.Visible||!form.setupNetwork.Enabled||form.setupNetwork.Checked)
                    throw new Exception("Setup network permissions must be visible and opt-in");
                var savedSetup=System.Text.Json.Nodes.JsonNode.Parse(json.RootElement.GetRawText())!.AsObject();
                savedSetup["network_setup_preferred"]=true;
                form.LoadSnapshot(JsonSerializer.SerializeToElement(savedSetup),acceptGraphics:true);
                if(!form.setupNetwork.Checked)throw new Exception("Setup did not restore the saved network preference");
                if(!form.setupHelp.Visible||!form.setupHelp.Enabled)throw new Exception("Setup help is unreachable");
                form.setupHelp.PerformClick();Application.DoEvents();
                if(!form.setupDetails.Visible||form.setupDetails.Text!=Lang.T("setupHelpIntro"))throw new Exception("Setup help requires a successful install");
                form.ShowError(new BackendError("operation_failed","Repair diagnostic fixture"));
                if(!form.setupDetails.Visible||!form.setupDetails.Text.Contains("Repair diagnostic fixture"))throw new Exception("Setup error details are hidden");
                form.testUiBackend=(action,_)=>Task.FromException<JsonElement>(new BackendError("package_invalid","private fixture path"));
                try
                {
                    var report=form.CollectDiagnostics().GetAwaiter().GetResult();
                    if(report["backend_status"]?.GetValue<string>()!="unavailable"||report.ToJsonString().Contains("private fixture path"))
                        throw new Exception("Setup diagnostics should remain exportable without the backend");
                }
                finally{form.testUiBackend=null;}
                using var setupImage=new Bitmap(form.Width,form.Height);form.DrawToBitmap(setupImage,new Rectangle(0,0,form.Width,form.Height));setupImage.Save(Path.Combine(folder,language+"-setup.png"));
                form.TestNativeGraphicsUi(folder,language);
                form.DiscardGraphicsDraft();form.Close();
                TestPrimaryActionScaling(json.RootElement,folder,language);
            }
            File.WriteAllText(Path.Combine(folder,"result.json"),JsonSerializer.Serialize(new {
                native_graphics_ui_checked=true,
                passed=true,checks=new[]{"translation_keys","release_version_exact_rc411_and_update_prefix_hidden","lan_port_save_and_restore","setup_help_and_error_details","setup_diagnostics_without_backend","setup_network_permission_opt_in","borderless_native_resolution","renderer_selection_and_draft","maximum_preset_uses_smaa_without_game_aa","exclusive_antialiasing_and_pipeline_options","compatibility_smaa_and_linked_lod_steps",
                    "graphics_presets_and_filtering","bilingual_pages_rendered","internet_role_choices_and_disclosure","navigation_ready_status","invitation_join_guard","internet_game_lifecycle_guidance",
                    "private_internet_controls","discord_community_and_share","discord_publisher_opt_in","minimum_window_pages_rendered","menus_fit_or_screen_height_limit","home_update_notice_and_busy_guard",
                    "easytier_consent_defaults_denied","discord_symbols_only_on_community_links","room_switch_requires_fresh_consent","four_play_mode_cards","mode_card_navigation_without_network_join",
                    "mode_card_accessibility_and_text_fit","four_modes_visible_at_minimum_size","public_hosting_without_discord_and_optional_listing",
                    "ingame_invitation_run_and_role_guards","invitation_clipboard_failure_and_private_public_separation",
                    "graphics_draft_save_discard_cancel","hardware_refresh_preserves_draft","local_refresh_stale_and_obsolete_results",
                    "language_and_cancel_io_error_boundaries","primary_actions_visible_at_minimum_size",
                    "public_join_actual_click_captures_before_busy","public_end_actual_click_cancels_while_busy",
                    "save_refresh_failure_discard_save_uses_committed_generation","unsupported_modern_presets_explained_and_disabled",
                    "settings_conflict_diagnostics_confirm_stale_token_restore_detach_journey","recovery_actions_visible_at_125_150_percent",
                    "primary_actions_at_125_150_percent_geometry_and_text_scale",
                    "mixed_package_gate_and_gold_host_history_guest_roles","mixed_host_requires_explicit_history_edition",
                    "mixed_private_share_roundtrip","mixed_language_preserves_edition_and_gold_graphics"},menu_layouts=menuLayouts,private_pipe_roundtrip=pipeProbe!=null&&testPython!=null}));return 0;
        }
        catch(Exception error){File.WriteAllText(Path.Combine(folder,"failure.txt"),error.ToString());return 2;}
    }
}

