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

internal static class Backend
{
    public static string? Operation { get; private set; }
    public static async Task<JsonElement> Run(string action, object data, Action<JsonElement> receive,bool trackOperation=true)
    {
        string operation = Guid.NewGuid().ToString("N"); if(trackOperation)Operation = operation;
        var start = new ProcessStartInfo(Path.Combine(Program.AppRoot,"Package","Runtime","python","python.exe"))
        {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardInput = true,
            RedirectStandardOutput = true, RedirectStandardError = true,
            StandardInputEncoding = new UTF8Encoding(false), StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8, WorkingDirectory = Program.AppRoot
        };
        foreach (string arg in new[] { "-B", "-u", Path.Combine(Program.AppRoot,"Package","Tools","installer","desktop_backend.py"), "--app", Program.AppRoot, "--operation", operation }) start.ArgumentList.Add(arg);
        using var process = Start(start);
        string request = JsonSerializer.Serialize(new { format = "s7-desktop-request-v1", action, data });
        if (Encoding.UTF8.GetByteCount(request) > 32768) throw new BackendError("request_invalid", "Request too large");
        await process.StandardInput.WriteAsync(request); process.StandardInput.Close();
        var errors = ReadBounded(process.StandardError, 8192);
        JsonElement result = default; BackendError? failure = null;
        try
        {
            while (await process.StandardOutput.ReadLineAsync() is string line)
            {
                if (line.Length > 131072) throw new BackendError("operation_failed", "Oversized backend event");
                using var json = JsonDocument.Parse(line); JsonElement item = json.RootElement.Clone();
                string? kind = item.GetProperty("kind").GetString();
                if (kind == "result") result = item.GetProperty("data").Clone();
                else if (kind == "error") failure = new BackendError(item.GetProperty("code").GetString()!, item.GetProperty("detail").GetString() ?? "");
                else receive(item);
            }
            await process.WaitForExitAsync(); string stderr = await errors;
            if (failure != null) throw failure;
            if (process.ExitCode != 0 || result.ValueKind == JsonValueKind.Undefined) throw new BackendError("operation_failed", stderr);
            return result;
        }
        finally { if (trackOperation && Operation == operation) Operation = null; }
    }
    private static async Task<string> ReadBounded(StreamReader source, int limit)
    {
        var value = new StringBuilder(); char[] buffer = new char[2048]; int count;
        while ((count = await source.ReadAsync(buffer)) > 0) if (value.Length < limit) value.Append(buffer,0,Math.Min(count,limit-value.Length));
        return value.ToString();
    }
    private static Process Start(ProcessStartInfo info)
    {
        try{return Process.Start(info)??throw new BackendError("package_invalid","Backend did not start");}
        catch(Exception error)when(error is IOException or System.ComponentModel.Win32Exception){throw new BackendError("package_invalid",error.Message);}
    }
    public static void Cancel()
    {
        if (Operation is string value && value.Length == 32)
        {
            string folder = Path.Combine(Program.AppRoot,"State");
            File.WriteAllText(Path.Combine(folder,"cancel-" + value), "cancel", Encoding.ASCII);
        }
    }
}

internal static class Shortcuts
{
    private static IEnumerable<string> Locations()
    {
        yield return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),"Settlers 7 WePlay Edition.lnk");
        yield return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu),"Programs","Settlers 7 WePlay Edition.lnk");
    }
    public static void Create()
    {
        Type? type = Type.GetTypeFromProgID("WScript.Shell"); if (type == null) return;
        dynamic shell;
        try{shell=Activator.CreateInstance(type)!;}catch(System.Runtime.InteropServices.COMException){return;}
        foreach (string path in Locations())
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

internal sealed class MainForm : Form
{
    private readonly bool testing;
    private bool busy, gameRunning, loading, forceSetup, recoverState, cancelRequested,updateExit;
    private bool updatesSupported,updateChecking,remoteUpdateAvailable;
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
    private readonly Label activeRenderer = new() { AutoSize = true };
    private readonly Dictionary<string,ComboBox> qualities = [];
    private readonly Dictionary<string,CheckBox> effects = [];
    private readonly Dictionary<Control,string> accessibleFields = [];
    private readonly TextBox details = new() { Multiline = true, ReadOnly = true, Dock = DockStyle.Top, Height = 120, ScrollBars = ScrollBars.Vertical };
    private readonly NotifyIcon tray = new() { Icon = SystemIcons.Application, Visible = false };
    private TabPage homePage = null!, lanPage = null!, internetPage = null!, graphicsPage = null!, helpPage = null!,updatesPage=null!;

    public MainForm(bool initialSetup = false, bool test = false)
    {
        testing = test; forceSetup = initialSetup;
        Text = Lang.T("app"); Font = new Font("Segoe UI",10); BackColor = Color.White;
        ClientSize = new Size(1020,720); MinimumSize = new Size(880,620); StartPosition = FormStartPosition.CenterScreen;
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
        installButton = Button("install",InstallGame,true); cancelButton = Button("cancel",() => { Backend.Cancel(); return Task.CompletedTask; }); cancelButton.Visible = false;
        cancelButton.Click += (_,_) => {Backend.Cancel();cancelRequested=true;cancelButton.Enabled=false;status.Text=Lang.T("cancelRequested");};
        BuildSetup(); BuildTabs();
        tabs.Visible=false;
        language.SelectedIndexChanged += (_,_) => { if (language.SelectedItem is Item item) { Lang.Set(item.Key,!testing); ApplyLanguage(); } };
        tray.DoubleClick += (_,_) => RestoreWindow();
        var trayMenu = new ContextMenuStrip(); trayMenu.Items.Add(Lang.T("show"),null,(_,_)=>RestoreWindow()); trayMenu.Items.Add(Lang.T("exit"),null,(_,_)=>Close()); tray.ContextMenuStrip = trayMenu;
        Resize += (_,_) => { if (gameRunning && WindowState == FormWindowState.Minimized) { ShowInTaskbar = false; tray.Visible = true; } };
        FormClosing += (_,e) =>
        {
            if(updateExit)return;
            if (gameRunning) { e.Cancel = true; WindowState = FormWindowState.Minimized; ShowInTaskbar = false; tray.Visible = true; }
            else if (busy) { e.Cancel = true; if (MessageBox.Show(this,Lang.T("closeBusy"),Lang.T("app"),MessageBoxButtons.YesNo,MessageBoxIcon.Question)==DialogResult.Yes) Backend.Cancel(); }
        };
        FormClosed += (_,_) => { tray.Dispose(); };
        Shown += async (_,_) => { if (!testing) await Initialise(); };
        Load += (_,_) =>
        {
            Rectangle area=Screen.FromControl(this).WorkingArea;
            MinimumSize=new Size(Math.Min(MinimumSize.Width,area.Width-16),Math.Min(MinimumSize.Height,area.Height-16));
            Size=new Size(Math.Min(Width,area.Width-16),Math.Min(Height,area.Height-16));
        };
        ApplyLanguage();
    }

    private static ComboBox Combo() => new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill, IntegralHeight = false, DropDownHeight = 280 };
    private void Bind(Control control,string key) { labels[control] = key; control.Text = Lang.T(key); }
    private Label Label(string key) { var result = new Label { AutoSize = true, Margin = new Padding(0,10,0,6), MaximumSize = new Size(900,0) }; Bind(result,key); return result; }
    private Button Button(string key,Func<Task> action,bool primary = false)
    {
        var result = new Button { AutoSize = true, MinimumSize = new Size(150,38), Padding = new Padding(10,3,10,3), Margin = new Padding(0,6,10,6), FlatStyle = FlatStyle.Flat };
        result.FlatAppearance.BorderColor = Color.FromArgb(190,200,213);
        if (primary) { result.BackColor = Color.FromArgb(30,91,164); result.ForeColor = Color.White; }
        Bind(result,key); result.Click += async (_,_) => await Guard(action); return result;
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
    private GroupBox StartCard(string title,string description,Button action)
    {
        var card=new GroupBox { AutoSize=true,Dock=DockStyle.Fill,MinimumSize=new Size(0,180),Margin=new Padding(0,8,12,8),Font=new Font(Font.FontFamily,11,FontStyle.Bold) };
        Bind(card,title);var content=Stack(card);content.Font=Font;content.Padding=new Padding(12,8,12,8);
        var explanation=Label(description);explanation.MaximumSize=new Size(230,0);Add(content,explanation);Add(content,Buttons(action));
        card.SizeChanged+=(_,_)=>explanation.MaximumSize=new Size(Math.Max(160,card.ClientSize.Width-36),0);
        return card;
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
        tabs.TabPages.AddRange([homePage,lanPage,internetPage,graphicsPage,helpPage,updatesPage]);
        var homeScroll=new Panel { Dock=DockStyle.Fill,AutoScroll=true };homePage.Controls.Add(homeScroll);
        var home = Stack(homeScroll); Add(home,homeTitle); Add(home,homeInfo);
        var starts=new TableLayoutPanel { AutoSize=true,Dock=DockStyle.Top,ColumnCount=3 };
        for(int column=0;column<3;column++)starts.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100f/3));
        starts.Controls.Add(StartCard("startLocalTitle","startLocalInfo",Button("playButton",()=>Play(false),true)),0,0);
        starts.Controls.Add(StartCard("startLanTitle","startLanInfo",Button("openLan",()=>{tabs.SelectedTab=lanPage;return Task.CompletedTask;})),1,0);
        starts.Controls.Add(StartCard("startInternetTitle","startInternetInfo",Button("openInternet",()=>{tabs.SelectedTab=internetPage;return Task.CompletedTask;})),2,0);
        Add(home,starts);Add(home,Buttons(Button("openGraphics",()=>{tabs.SelectedTab=graphicsPage;return Task.CompletedTask;})));
        Add(home,homeUpdateNotice);
        homeUpdateInstall=Button("updateInstall",InstallUpdate,true);
        Add(home,Buttons(Button("updates",()=>{tabs.SelectedTab=updatesPage;return Task.CompletedTask;}),homeUpdateInstall));
        var lanScroll=new Panel { Dock=DockStyle.Fill,AutoScroll=true };lanPage.Controls.Add(lanScroll);var lan = Stack(lanScroll);
        var lanTitle=Label("lanTitle");lanTitle.Font=new Font(Font.FontFamily,20,FontStyle.Bold);Add(lan,lanTitle);
        Add(lan,Label("lanIntro"));Add(lan,Field("player",player));
        Add(lan,Buttons(Button("lanStart",()=>Play(true),true)));
        Add(lan,Label("lanSteps"));Add(lan,Label("lanTrouble"));
        Add(lan,Buttons(Button("lanPermission",Firewall)));Add(lan,lanNotice);
        var internetScroll = new Panel { Dock=DockStyle.Fill, AutoScroll=true }; internetPage.Controls.Add(internetScroll);
        var net = Stack(internetScroll);
        var netTitle = Label("internetTitle"); netTitle.Font=new Font(Font.FontFamily,20,FontStyle.Bold); Add(net,netTitle);
        Add(net,Label("internetIntro"));Bind(internetUnavailable,"internetUnavailable");Add(net,internetUnavailable);
        Add(net,internetConnection);Add(net,internetNext);
        void AddSetup(Control control){internetSetupControls.Add(control);Add(net,control);}
        AddSetup(Field("player",internetPlayer));
        Bind(internetHostChoice,"internetHostChoice");Bind(internetGuestChoice,"internetGuestChoice");internetHostChoice.Checked=true;
        AddSetup(Buttons(internetHostChoice,internetGuestChoice));
        internetHostPanel=new TableLayoutPanel { AutoSize=true,Dock=DockStyle.Top,ColumnCount=1 };internetHostPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        accessibleFields[internetHostPanel]="internetHostChoice";
        Add(internetHostPanel,Label("internetHostSteps"));
        internetCreate=Button("internetCreate",()=>PlayInternet(true),true);Add(internetHostPanel,Buttons(internetCreate));
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
        internetJoinButton=Button("internetJoin",()=>PlayInternet(false),true);Add(internetGuestPanel,Buttons(internetJoinButton));AddSetup(internetGuestPanel);
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
            if(internetOperation&&!gameRunning){Backend.Cancel();cancelRequested=true;internetEnd.Enabled=false;status.Text=Lang.T("cancelRequested");}
        };
        internetEndRow=Buttons(internetEnd);Add(net,internetEndRow); Add(net,Label("internetSafety"));
        var discordSection=new GroupBox { AutoSize=true,Dock=DockStyle.Top,Margin=new Padding(0,14,0,8),Font=new Font(Font.FontFamily,11,FontStyle.Bold) };
        Bind(discordSection,"discordOptionalTitle");var discord=Stack(discordSection);discord.Font=Font;
        Add(discord,Label("discordOptionalInfo"));Add(discord,discordNotice);
        Bind(discordOptions,"discordOptions");Add(discord,Buttons(Button("discordFindPlayers",()=>{Community.OpenDiscord();return Task.CompletedTask;}),discordOptions));
        discordOptionsPanel=new TableLayoutPanel { AutoSize=true,Dock=DockStyle.Top,ColumnCount=1 };discordOptionsPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        accessibleFields[discordOptionsPanel]="discordOptions";
        Add(discordOptionsPanel,Label("discordSetupSteps"));
        discordConnect=Button("discordConnect",ConnectDiscord);discordDisconnect=Button("discordDisconnect",DisconnectDiscord);
        Add(discordOptionsPanel,Buttons(discordConnect,discordDisconnect));Bind(discordPublish,"discordPublish");Add(discordOptionsPanel,discordPublish);
        Add(discord,discordOptionsPanel);Add(net,discordSection);
        internetHostChoice.CheckedChanged+=(_,_)=>UpdateInternetControls();internetGuestChoice.CheckedChanged+=(_,_)=>UpdateInternetControls();
        internetAdvanced.CheckedChanged+=(_,_)=>{UpdateInternetControls();if(internetAdvanced.Checked)internetScroll.ScrollControlIntoView(internetAdvancedPanel);};
        discordOptions.CheckedChanged+=(_,_)=>{UpdateInternetControls();if(discordOptions.Checked)internetScroll.ScrollControlIntoView(discordOptionsPanel);};
        internetJoin.TextChanged+=(_,_)=>UpdateInternetControls();discordPublish.CheckedChanged+=(_,_)=>UpdateInternetControls();
        internetMode.SelectedIndexChanged += (_,_)=>UpdateInternetControls();
        var graphicScroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true }; graphicsPage.Controls.Add(graphicScroll); var g = Stack(graphicScroll);
        Add(g,Label("rendererInfo"));Add(g,Label("graphicsModeInfo")); Add(g,activeRenderer);
        Add(g,Field("renderer",renderer)); Add(g,Field("gpu",gpu)); Bind(keepDisplay,"keepDisplay"); Add(g,keepDisplay);
        var outputs = new TableLayoutPanel { AutoSize = true, Dock = DockStyle.Top, ColumnCount = 2 };
        outputs.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50)); outputs.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));
        outputs.Controls.Add(Field("display",monitor),0,0); outputs.Controls.Add(Field("window",window),1,0);
        outputs.Controls.Add(Field("resolution",resolution),0,1); Add(g,outputs); Add(g,Label("borderlessInfo"));
        var qualityGrid = new TableLayoutPanel { AutoSize = true, Dock = DockStyle.Top, ColumnCount = 2 };
        qualityGrid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50)); qualityGrid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));
        string[] levels = ["ShaderQuality","TextureQuality","ObjectQuality","DistanceCulling","ShadowQuality","WaterQuality","ClutterQuality"];
        for (int i=0;i<levels.Length;i++) { var combo = Combo(); qualities[levels[i]]=combo; qualityGrid.Controls.Add(Field(levels[i],combo),i%2,i/2); }
        Add(g,Label("quality"));
        Add(g,Buttons(Button("qualityClear",()=>{ApplyQualityPreset(false);return Task.CompletedTask;}),
            Button("qualityDetail",()=>{ApplyQualityPreset(true);return Task.CompletedTask;})));
        Add(g,Label("qualityPresetInfo")); Add(g,qualityGrid);
        var effectRow = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Top, WrapContents = true };
        foreach (string key in new[] { "Shadows","SSAO","DepthOfField","SunShafts","Parallax","Antialiasing" })
        { var box = new CheckBox { AutoSize = true, Margin = new Padding(0,8,18,8) }; Bind(box,key); effects[key]=box; effectRow.Controls.Add(box); }
        Add(g,effectRow); Bind(anisotropicFiltering,"anisotropicFiltering");Add(g,anisotropicFiltering);
        Add(g,Label("anisotropicInfo"));Bind(simple,"simple"); Add(g,simple);
        var saveRow=Buttons(Button("save",SaveGraphics,true));saveRow.Dock=DockStyle.Bottom;saveRow.Padding=new Padding(14,0,14,0);graphicsPage.Controls.Add(saveRow);
        monitor.SelectedIndexChanged += (_,_) => { if (!loading) UpdateResolutions(); };
        window.SelectedIndexChanged += (_,_) => { if (!loading) UpdateResolutions(); };
        keepDisplay.CheckedChanged += (_,_) => DisplayEnabled(); renderer.SelectedIndexChanged += (_,_) => DisplayEnabled();
        var helpScroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true }; helpPage.Controls.Add(helpScroll); var h = Stack(helpScroll);
        Add(h,Label("helpIntro"));
        var ways=Label("startGuideTitle");ways.Font=new Font(Font.FontFamily,12,FontStyle.Bold);Add(h,ways);
        Add(h,Label("startGuide"));Add(h,Label("roomGuide"));Add(h,Label("invitationGuide"));
        var maintenance=Label("maintenanceTitle");maintenance.Font=new Font(Font.FontFamily,12,FontStyle.Bold);Add(h,maintenance);
        Add(h,Label("maintenanceGuide"));
        Add(h,Buttons(Button("verify",Verify),Button("repair",ShowRepair)));
        Add(h,Buttons(Button("fallback",Fallback),Button("probe",Probe),Button("lanPermission",Firewall)));
        Add(h,Buttons(Button("cleanup",Cleanup),Button("diagnostics",Diagnostics))); Add(h,Label("details")); Add(h,details);
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
    }

    private void ApplyLanguage()
    {
        foreach (var item in labels) item.Key.Text = Lang.T(item.Value);
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
            else { ShowMain(); Shortcuts.Create(); }
        }
        catch(Exception error)
        {
            ShowError(error);setup.Visible=true;tabs.Visible=false;installButton.Enabled=true;
            if(error is BackendError failure&&failure.Code=="installation_invalid"){recoverState=true;Bind(installButton,"restartSetup");}
        }
        finally {SetBusy(false);}
        if(!testing&&updatesSupported)_=CheckUpdates(true);
    }
    private void LoadSnapshot(JsonElement value,bool replaceFields=true)
    {
        snapshot=value.Clone();loading=true;
        updatesSupported=value.TryGetProperty("updates_supported",out var updatesAvailable)&&updatesAvailable.GetBoolean();
        lastUpdateFailure=value.TryGetProperty("update_last_error",out var updateFailure)&&updateFailure.ValueKind==JsonValueKind.String?updateFailure.GetString():null;
        screens.Clear();screens.AddRange(value.GetProperty("displays").EnumerateArray().Select(x=>x.Clone()));
        if(replaceFields)
        {
            destination.Text=value.GetProperty("data_default").GetString(); player.Text=value.GetProperty("player_name").GetString();internetPlayer.Text=player.Text;
            string policy=value.GetProperty("renderer_policy").GetString()!;renderer.SelectedIndex=policy=="dxvk"?1:policy=="native"?2:0;
            simple.Checked=value.GetProperty("choice").TryGetProperty("simple_menu",out var simpleValue)&&simpleValue.GetBoolean();
            anisotropicFiltering.Checked=value.GetProperty("choice").TryGetProperty("anisotropic_filtering",out var filteringValue)&&filteringValue.GetBoolean();
            var quality=value.GetProperty("quality");foreach(var pair in qualities)pair.Value.SelectedIndex=quality.GetProperty(pair.Key).GetInt32();
            foreach(var pair in effects)pair.Value.Checked=quality.GetProperty(pair.Key).GetInt32()!=0;
        }
        internetAvailable=value.TryGetProperty("internet_available",out var available)&&available.GetBoolean();
        discordAvailable=value.TryGetProperty("discord_available",out var directoryAvailable)&&directoryAvailable.GetBoolean();
        discordConnected=value.TryGetProperty("discord_connected",out var connected)&&connected.GetBoolean();
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
        loading=false;UpdateResolutions();
        if(replaceFields&&selected.ValueKind==JsonValueKind.Object&&window.SelectedIndex==0)
        {
            string key=selected.GetProperty("width")+"x"+selected.GetProperty("height");
            for(int i=0;i<resolution.Items.Count;i++)if(resolution.Items[i] is Item option&&option.Key==key)resolution.SelectedIndex=i;
        }
    }
    private void ShowMain(){setup.Visible=false;tabs.Visible=true;forceSetup=false;homeTitle.Text=Lang.T("playTitle");homeInfo.Text=Lang.T("playIntro");status.Text=Lang.T("ready");}
    private async Task ShowSetup()
    {
        tabs.Visible=false;setup.Visible=true;
        Bind(installButton,snapshot.GetProperty("installed").GetBoolean()?"update":"install");
        source.Items.Clear();
        Bind(setupNotice,"detecting");var found=await Backend.Run("detect",new {},Receive);
        foreach(var item in found.GetProperty("sources").EnumerateArray())
        {
            string path=item.GetProperty("path").GetString()!;
            if(!source.Items.Cast<Item>().Any(x=>x.Key==path))source.Items.Add(new Item(path,path));
        }
        if(source.Items.Count>0){source.SelectedIndex=0;Bind(setupNotice,"found");status.Text=Lang.T("found");}else {Bind(setupNotice,"notFound");status.Text=Lang.T("notFound");}
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
        updateNotice.Text=homeUpdateNotice.Text=string.Format(Lang.T(key),remoteUpdateVersion);
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
        updateNotes.Text=value.GetProperty("notes").GetString()??"";RefreshUpdateControls();
    }
    private async Task CheckUpdates(bool automatic)
    {
        if(!updatesSupported||updateChecking)return;
        updateChecking=true;updateNotice.Text=homeUpdateNotice.Text=Lang.T("updateChecking");RefreshUpdateControls();
        try
        {
            var value=await Backend.Run("update-check",new {},_=>{},trackOperation:false);
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
        await Backend.Run("update-install",new {sequence=remoteUpdateSequence},Receive);
        updateExit=true;Close();
    }
    private void UpdateResolutions()
    {
        if(loading)return;
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
    }
    private void ApplyQualityPreset(bool maximumObjects)
    {
        foreach(var pair in qualities)pair.Value.SelectedIndex=pair.Key=="ObjectQuality"&&!maximumObjects?1:2;
        foreach(var pair in effects)pair.Value.Checked=pair.Key!="DepthOfField";
        anisotropicFiltering.Checked=true;
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
            anisotropic_filtering=anisotropicFiltering.Checked},quality,player_name=ValidName()};
    }
    private string ValidName()
    {
        string name=player.Text;
        if(name.Trim()!=name||Encoding.UTF8.GetByteCount(name) is <1 or >24||name.EnumerateRunes().Any(r=>
            r.Value is 0x2028 or 0x2029||Rune.GetUnicodeCategory(r) is UnicodeCategory.Control or UnicodeCategory.Format or UnicodeCategory.PrivateUse or UnicodeCategory.OtherNotAssigned or UnicodeCategory.Surrogate))throw new BackendError("nameInvalid","");
        return name;
    }
    private async Task SaveGraphics(){LoadSnapshot(await Backend.Run("save",GraphicsRequest(),Receive));status.Text=Lang.T("saved");}
    private async Task Fallback(){LoadSnapshot(await Backend.Run("save",GraphicsRequest(true),Receive));status.Text=Lang.T("saved");}
    private async Task Probe(){await Backend.Run("probe",new {},Receive);LoadSnapshot(await Backend.Run("snapshot",new {},Receive));}
    private async Task Verify(){await Backend.Run("verify",new {},Receive);status.Text=Lang.T("verified");}
    private async Task ShowRepair(){forceSetup=true;await ShowSetup();}
    private async Task Cleanup()
    {
        if(MessageBox.Show(this,Lang.T("cleanupConfirm"),Lang.T("app"),MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
        await Backend.Run("cleanup",new {},Receive);status.Text=Lang.T("cleanupDone");
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
        if(lan)await Backend.Run("lan-play",new {player_name=ValidName()},Receive);
        else await Backend.Run("play",new {},Receive);
        gameRunning=false;RestoreWindow();homeTitle.Text=Lang.T("playTitle");homeInfo.Text=Lang.T("playIntro");LoadSnapshot(await Backend.Run("snapshot",new {},Receive));status.Text=Lang.T("ready");
    }
    private void UpdateInternetControls()
    {
        bool direct=internetMode.SelectedIndex==1;
        internetPolicy.Text=Lang.T(direct?"internetDirectInfo":"internetAssistedInfo");
        internetConnection.Text=Lang.T(!internetOperation&&internetGuestChoice.Checked?"internetConnectionFromInvite":
            direct?"internetConnectionDirect":"internetConnectionAuto");
        internetUnavailable.Visible=!internetAvailable;
        internetNext.Visible=internetOperation;
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
        if(internetCopy!=null)internetCopy.Enabled=internetOperation&&internetInvite.Text.Length>0;
        if(internetShare!=null)internetShare.Enabled=internetOperation&&internetInvite.Text.Length>0;
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
        if(internetInviteField!=null)internetInviteField.Visible=internetOperation&&internetInvite.Text.Length>0;
        if(internetShareRow!=null)internetShareRow.Visible=internetOperation&&internetInvite.Text.Length>0;
        if(internetEndRow!=null)internetEndRow.Visible=internetOperation;
        internetInvite.Enabled=true;
        discordPublish.Enabled=!busy&&discordAvailable&&discordConnected;discordPublish.Visible=discordAvailable;
        if(discordConnect!=null)discordConnect.Enabled=!busy&&discordAvailable;
        if(discordDisconnect!=null)discordDisconnect.Enabled=!busy&&discordAvailable&&discordConnected;
        if(discordOptionsPanel!=null)discordOptionsPanel.Visible=discordOptions.Checked;
        discordNotice.Text=Lang.T(!discordAvailable?"discordUnavailableInfo":!discordConnected?"discordNotConnected":discordPublish.Checked?"discordConnected":"discordConnectedPrivate");
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
    private async Task PlayInternet(bool create)
    {
        player.Text=internetPlayer.Text;string name=ValidName();
        string? invitation=create?null:Community.InvitationCode(internetJoin.Text);
        if(!create&&(string.IsNullOrWhiteSpace(invitation)||invitation.Length>2048))throw new BackendError("internet_invitation_invalid","");
        bool direct=internetMode.SelectedIndex==1;
        string? endpoint=direct?internetEndpoint.Text:null;
        internetCreating=create;internetStage="connecting";internetOperation=true;internetInvite.Clear();internetJoin.Clear();SetBusy(true);tabs.SelectedTab=internetPage;
        using var channel=new InternetChannel();
        Task privateTask=channel.Run(invitation,code=>
        {
            void Accept(){if(internetOperation){internetInvite.Text=code;internetStage="ready";UpdateInternetControls();}}
            if(InvokeRequired)BeginInvoke(new Action(Accept));else Accept();
        });
        try
        {
            if(create)await Backend.Run("internet-create",new {player_name=name,channel=channel.Name,mode=direct?"direct":"assisted",direct_endpoint=endpoint,publish_discord=discordPublish.Checked},Receive);
            else await Backend.Run("internet-join",new {player_name=name,channel=channel.Name,publish_discord=discordPublish.Checked},Receive);
            channel.Dispose();await privateTask;
            gameRunning=false;homeTitle.Text=Lang.T("playTitle");homeInfo.Text=Lang.T("playIntro");RestoreWindow();LoadSnapshot(await Backend.Run("snapshot",new {},Receive));status.Text=Lang.T("ready");
        }
        finally
        {
            channel.Dispose();
            try{await privateTask;}catch(Exception){/* No private bytes enter exception details. */}
            internetInvite.Clear();internetOperation=false;UpdateInternetControls();
        }
    }
    private void Receive(JsonElement value)
    {
        if(InvokeRequired){BeginInvoke(new Action(()=>Receive(value)));return;}
        string? kind=value.GetProperty("kind").GetString();
        if(kind=="progress")
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
            gameRunning=true;homeTitle.Text=Lang.T("running");homeInfo.Text=Lang.T("runningInfo");status.Text=Lang.T("running");progress.Visible=false;
            if(internetOperation)internetStage="playing";UpdateInternetControls();
        }
        else if(kind=="game_finished")
        {
            gameRunning=false;homeTitle.Text=Lang.T("playTitle");homeInfo.Text=Lang.T("playIntro");
            if(internetOperation)internetStage="hosting";status.Text=Lang.T("ready");UpdateInternetControls();
        }
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
        busy=value;tabs.Enabled=!value||internetOperation;source.Enabled=destination.Enabled=installButton.Enabled=!value;language.Enabled=!value;
        foreach(TabPage page in tabs.TabPages)page.Enabled=!value||(internetOperation&&page==internetPage);
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
    }
    private static IEnumerable<Control> Descendants(Control parent)
    { foreach(Control control in parent.Controls){yield return control;foreach(var child in Descendants(control))yield return child;} }
    private async Task Guard(Func<Task> action)
    {
        if(busy)return;
        SetBusy(true);
        try{await action();}
        catch(Exception error){gameRunning=false;RestoreWindow();ShowError(error);}
        finally{SetBusy(false);}
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
                Lang.Set(language,false);using var form=new MainForm(false,true);form.StartPosition=FormStartPosition.Manual;form.Location=new Point(-30000,-30000);form.ShowInTaskbar=false;
                if(form.releaseVersion.Text!=Program.ReleaseVersion||!form.releaseVersion.Text.StartsWith("2."))throw new Exception("Release version label differs from assembly metadata");
                form.LoadSnapshot(json.RootElement);form.ShowMain();form.Show();Application.DoEvents();
                using var request=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest()));
                var output=request.RootElement.GetProperty("choice").GetProperty("display");
                if(output.GetProperty("mode").GetString()!="borderless"||output.GetProperty("width").GetInt32()!=1920||form.resolution.Enabled)throw new Exception("Borderless controls failed");
                if(form.anisotropicFiltering.Checked)throw new Exception("Legacy preferences force texture filtering");
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
                form.LoadSnapshot(json.RootElement);
                form.renderer.SelectedIndex=2;
                using var compatible=JsonDocument.Parse(JsonSerializer.Serialize(form.GraphicsRequest()));
                if(compatible.RootElement.GetProperty("choice").GetProperty("choice").ValueKind!=JsonValueKind.Null)throw new Exception("Native GPU filtering enabled");
                if(form.anisotropicFiltering.Enabled)throw new Exception("Native texture filtering control enabled");
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
                form.discordOptions.Checked=true;Application.DoEvents();
                if(form.discordPublish.Enabled||!form.discordConnect.Enabled||!form.discordOptionsPanel.Visible||form.discordNotice.Text!=Lang.T("discordNotConnected"))
                    throw new Exception("Private Internet play requires Discord");
                Capture("discord-options");form.discordOptions.Checked=false;
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
                form.discordAvailable=true;form.discordConnected=true;form.discordPublish.Checked=true;form.UpdateInternetControls();
                if(!form.discordPublish.Enabled||!form.discordDisconnect.Enabled||!form.discordConnect.Enabled)
                    throw new Exception("Connected Discord publisher controls failed");
                form.discordPublish.Checked=false;
                if(form.discordNotice.Text!=Lang.T("discordConnectedPrivate"))throw new Exception("Disabled Discord publication is unclear");
                form.discordAvailable=false;form.UpdateInternetControls();
                if(form.discordConnect.Enabled||form.discordPublish.Visible||form.discordNotice.Text!=Lang.T("discordUnavailableInfo"))
                    throw new Exception("Unavailable Discord capability controls failed");
                form.discordAvailable=true;form.UpdateInternetControls();
                foreach(var page in new[]{form.homePage,form.lanPage,form.internetPage,form.graphicsPage,form.helpPage,form.updatesPage})
                {
                    form.tabs.SelectedTab=page;Application.DoEvents();using var bitmap=new Bitmap(form.Width,form.Height);
                    form.DrawToBitmap(bitmap,new Rectangle(0,0,form.Width,form.Height));bitmap.Save(Path.Combine(folder,language+"-"+form.tabs.SelectedIndex+".png"));
                }
                form.tabs.SelectedTab=form.graphicsPage;form.renderer.SelectedIndex=0;form.ApplyQualityPreset(false);
                var graphicsScroll=form.graphicsPage.Controls.OfType<Panel>().Single(panel=>panel.AutoScroll);
                graphicsScroll.ScrollControlIntoView(form.qualities["ShaderQuality"]);Capture("graphics-quality");
                graphicsScroll.ScrollControlIntoView(form.anisotropicFiltering);Capture("graphics-effects");
                Size normalSize=form.Size;form.Size=form.MinimumSize;
                foreach(var page in new[]{form.homePage,form.internetPage,form.graphicsPage})
                {form.tabs.SelectedTab=page;Capture("minimum-"+form.tabs.SelectedIndex);}
                form.Size=normalSize;
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
                form.Close();
            }
            File.WriteAllText(Path.Combine(folder,"result.json"),JsonSerializer.Serialize(new {
                passed=true,checks=new[]{"translation_keys","release_version_from_assembly","borderless_native_resolution","native_gpu_filter_disabled",
                    "graphics_presets_and_filtering","bilingual_pages_rendered","internet_role_choices_and_disclosure","navigation_ready_status","invitation_join_guard","internet_game_lifecycle_guidance",
                    "private_internet_controls","discord_community_and_share","discord_publisher_opt_in","minimum_window_pages_rendered","home_update_notice_and_busy_guard"},private_pipe_roundtrip=pipeProbe!=null&&testPython!=null}));return 0;
        }
        catch(Exception error){File.WriteAllText(Path.Combine(folder,"failure.txt"),error.ToString());return 2;}
    }
}

