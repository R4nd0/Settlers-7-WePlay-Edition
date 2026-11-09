using System.Diagnostics;
using System.Security.Cryptography;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace S7Desktop;

// The timing override is internal and used only by the linked-source test
// executable. Production policy never accepts timeouts from environment/input.
internal sealed record BackendTiming(TimeSpan Idle,TimeSpan Grace,TimeSpan Poll)
{
    internal static BackendTiming For(string action)=>new(action switch {
        "directory-list" or "lan-list" or "recovery-status"=>TimeSpan.FromSeconds(60),
        "update-check"=>TimeSpan.FromMinutes(3),
        "detect" or "verify"=>TimeSpan.FromMinutes(10),
        _=>TimeSpan.FromMinutes(3)},TimeSpan.FromSeconds(5),TimeSpan.FromMilliseconds(100));
}

internal static class Backend
{
    private static readonly object operationsLock=new();
    private static Session? operation;
    // These are dispatched before installation recovery. Other actions can
    // recover/mutate and remain cooperative-only even before a state frame.
    private static readonly HashSet<string> ReadOnlyActions=["directory-list","update-check","recovery-status"];
    public static string? Operation {get{lock(operationsLock)return operation?.Id;}}

    public static Task<JsonElement> Run(string action,object data,Action<JsonElement> receive,bool trackOperation=true,CancellationToken cancellationToken=default)
    {
        var start=new ProcessStartInfo(Path.Combine(Program.AppRoot,"Package","Runtime","python","python.exe")) {
            UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true,
            StandardInputEncoding=new UTF8Encoding(false),StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8,
            WorkingDirectory=Program.AppRoot};
        string id=Guid.NewGuid().ToString("N");
        foreach(string arg in new[]{"-B","-u",Path.Combine(Program.AppRoot,"Package","Tools","installer","desktop_backend.py"),"--app",Program.AppRoot,"--operation",id})start.ArgumentList.Add(arg);
        return RunProcess(start,Program.AppRoot,id,action,data,receive,trackOperation,cancellationToken);
    }

    internal static async Task<JsonElement> RunProcess(ProcessStartInfo start,string root,string id,string action,object data,
        Action<JsonElement> receive,bool trackOperation,CancellationToken cancellationToken,BackendTiming? timing=null)
    {
        string request=JsonSerializer.Serialize(new{format="s7-desktop-request-v1",action,data});
        if(Encoding.UTF8.GetByteCount(request)>32768)throw new BackendError("request_invalid","Request too large");
        var session=new Session(root,id,ReadOnlyActions.Contains(action),timing??BackendTiming.For(action));
        if(trackOperation)lock(operationsLock) {
            if(operation!=null)throw new BackendError("operation_busy","");
            operation=session;
        }
        using var finished=new CancellationTokenSource();using var readStop=new CancellationTokenSource();
        Process? process=null;Task? watch=null;Task<string>? errors=null;Task<JsonElement>? pump=null;
        try {
            session.Expect(start.FileName);process=Start(start);session.Own(process);
            errors=ReadBounded(process.StandardError,8192,readStop.Token);
            pump=Pump();watch=Watch();
            Task winner=await Task.WhenAny(pump,watch);
            if(winner==watch)await watch;
            try{return await pump;}
            catch(OperationCanceledException) when(session.TerminalCode!=null){throw new BackendError(session.TerminalCode!,"");}

            async Task<JsonElement> Pump() {
                BackendError? failure=null;JsonElement result=default;
                await process.StandardInput.WriteAsync(request);process.StandardInput.Close();
                var lines=new BoundedLines(process.StandardOutput,131072);
                try {
                    while(await lines.Read(readStop.Token) is string line) {
                        session.Touch();
                        using var document=JsonDocument.Parse(line);var item=document.RootElement.Clone();
                        string? kind=item.GetProperty("kind").GetString();session.Observe(item,kind);
                        if(kind=="result")result=item.GetProperty("data").Clone();
                        else if(kind=="error")failure=new BackendError(item.GetProperty("code").GetString()!,item.GetProperty("detail").GetString()??"");
                        else receive(item);
                    }
                }
                catch(Exception error) when(error is JsonException or KeyNotFoundException or InvalidOperationException or BackendError) {
                    failure=new BackendError("backend_protocol_invalid","");session.ProtocolFailure=true;
                    try{session.RequestCancel();}catch(BackendError){}
                    // Never close a mutation's stdout while it is committing.
                    // Continue bounded-memory draining until it safely exits.
                    await ReadBounded(process.StandardOutput,0,readStop.Token);
                }
                await process.WaitForExitAsync(readStop.Token);string stderr=await errors;
                if(failure!=null)throw failure;
                if(process.ExitCode!=0||result.ValueKind==JsonValueKind.Undefined)throw new BackendError("operation_failed",stderr);
                return result;
            }

            async Task Watch() {
                while(!finished.IsCancellationRequested) {
                    await Task.Delay(session.Timing.Poll,finished.Token);
                    if(pump!.IsCompleted)return;
                    if(cancellationToken.IsCancellationRequested)RequestCancellation();
                    if(session.LiveGame)continue; // An intentionally running game has no request deadline.
                    bool expired=session.IdleElapsed>=session.Timing.Idle;
                    if(expired&&!session.Critical)RequestCancellation();
                    if(session.Critical) {
                        if(expired||session.Cancelled)Warn();
                        continue;
                    }
                    if(session.Cancelled&&session.CancelElapsed>=session.Timing.Grace) {
                        if(session.CanTerminate) {
                            if(!session.StopOwned()) {Warn();continue;}
                            session.TerminalCode=session.ProtocolFailure?"backend_protocol_invalid":cancellationToken.IsCancellationRequested?"cancelled":"backend_timeout";
                            readStop.Cancel();
                            try{await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));}catch(TimeoutException){Warn();continue;}
                            throw new BackendError(session.TerminalCode,"");
                        }
                        Warn(); // Mutations stay owned/drained; their recovery journal is authoritative.
                    }
                }
                void Warn() {
                    if(!session.MarkWarning())return;
                    try{using var json=JsonDocument.Parse("{\"kind\":\"warning\",\"code\":\"backend_unresponsive\"}");receive(json.RootElement.Clone());}catch(Exception){}
                }
                void RequestCancellation(){try{session.RequestCancel();}catch(BackendError){Warn();}}
            }
        }
        catch {
            if(process!=null&&!process.HasExited) {
                try{process.StandardInput.Close();}catch(IOException){}
                try{session.RequestCancel();}catch(BackendError){}
                if(session.CanTerminate) {
                    await Task.Delay(session.Timing.Grace);
                    if(session.StopOwned())readStop.Cancel();
                }
                // Do not sever an active mutation's stdout after a framing,
                // callback or input error. Its own recovery remains in charge.
                if(!process.HasExited) {
                    await ReadBounded(process.StandardOutput,0,CancellationToken.None);
                    await process.WaitForExitAsync();
                }
            }
            throw;
        }
        finally {
            finished.Cancel();
            if(watch!=null)try{await watch;}catch(OperationCanceledException){}catch(BackendError){}
            // A short command stopped by the watchdog can leave its cancelled
            // read task completing asynchronously. Observe it before disposing.
            if(pump!=null&&!pump.IsCompleted&&readStop.IsCancellationRequested)try{await pump;}catch(Exception){}
            if(errors!=null&&readStop.IsCancellationRequested)try{await errors;}catch(Exception){}
            process?.Dispose();
            session.CleanupCancel();
            if(trackOperation)lock(operationsLock){if(ReferenceEquals(operation,session))operation=null;}
        }
    }

    internal static Task<string?> ReadLineBounded(StreamReader source,int limit,CancellationToken token)=>new BoundedLines(source,limit).Read(token);

    private sealed class BoundedLines(StreamReader source,int limit)
    {
        private readonly char[] buffer=new char[2048];private int position,count;
        internal async Task<string?> Read(CancellationToken token) {
            var line=new StringBuilder(Math.Min(limit,4096));
            while(true) {
                if(position==count){count=await source.ReadAsync(buffer.AsMemory(),token);position=0;if(count==0)return line.Length==0?null:line.ToString().TrimEnd('\r');}
                int end=Array.IndexOf(buffer,'\n',position,count-position);
                int take=(end<0?count:end)-position;
                if(take>limit-line.Length)throw new BackendError("backend_protocol_invalid","");
                line.Append(buffer,position,take);position+=take;
                if(end>=0){position++;return line.ToString().TrimEnd('\r');}
            }
        }
    }

    private static async Task<string> ReadBounded(StreamReader source,int limit,CancellationToken token)
    {
        var value=new StringBuilder();char[] buffer=new char[2048];int count;
        while((count=await source.ReadAsync(buffer.AsMemory(),token))>0)
            if(value.Length<limit)value.Append(buffer,0,Math.Min(count,limit-value.Length));
        return value.ToString();
    }
    private static Process Start(ProcessStartInfo info) {
        try{return Process.Start(info)??throw new BackendError("package_invalid","");}
        catch(Exception error) when(error is IOException or System.ComponentModel.Win32Exception){throw new BackendError("package_invalid",error.Message);}
    }
    public static void Cancel() {
        Session? current;lock(operationsLock)current=operation;
        current?.RequestCancel();
    }

    private sealed class Session(string root,string id,bool canTerminate,BackendTiming timing)
    {
        private readonly object gate=new();private readonly Stopwatch clock=Stopwatch.StartNew();
        private TimeSpan lastActivity,requested;private bool cancelled,cancelDelivered,warning,critical,live,protocolFailure;
        private Process? process;private int pid;private DateTime started;private string? image,hash,terminalCode;
        internal string? TerminalCode {get{lock(gate)return terminalCode;}set{lock(gate)terminalCode=value;}}
        internal string Id=>id;internal BackendTiming Timing=>timing;
        internal bool CanTerminate=>canTerminate;
        internal bool Critical {get{lock(gate)return critical;}}
        internal bool LiveGame {get{lock(gate)return live;}}
        internal bool Cancelled {get{lock(gate)return cancelled;}}
        internal bool ProtocolFailure {get{lock(gate)return protocolFailure;}set{lock(gate)protocolFailure=value;}}
        internal TimeSpan IdleElapsed {get{lock(gate)return clock.Elapsed-lastActivity;}}
        internal TimeSpan CancelElapsed {get{lock(gate)return clock.Elapsed-requested;}}
        internal void Touch(){lock(gate)lastActivity=clock.Elapsed;}
        internal bool MarkWarning(){lock(gate){if(warning)return false;warning=true;return true;}}
        internal void Observe(JsonElement item,string? kind) {
            lock(gate) {
                if(kind=="operation_state") {
                    string? phase=item.GetProperty("phase").GetString();
                    if(phase is not ("cancellable" or "critical" or "live_game" or "idle"))throw new BackendError("backend_protocol_invalid","");
                    critical=phase=="critical";live=phase=="live_game";
                }
                else if(kind=="game_started")live=true;
                else if(kind=="game_finished")live=false;
                else if(kind=="progress"&&item.GetProperty("phase").GetString()=="patch")critical=true;
            }
        }
        internal void RequestCancel() {
            lock(gate) {
                if(cancelDelivered)return;
                if(!cancelled){cancelled=true;requested=clock.Elapsed;}
                try {
                    string state=Path.Combine(root,"State");Directory.CreateDirectory(state);
                    if((File.GetAttributes(state)&FileAttributes.ReparsePoint)!=0)throw new IOException();
                    string target=Path.Combine(state,"cancel-"+id);
                    if(File.Exists(target)&&(File.GetAttributes(target)&FileAttributes.ReparsePoint)!=0)throw new IOException();
                    File.WriteAllText(target,"cancel",Encoding.ASCII);
                }
                catch(Exception error) when(error is IOException or UnauthorizedAccessException){throw new BackendError("cancel_request_failed","");}
                cancelDelivered=true;
            }
        }
        internal void Expect(string executable) {
            try{image=Path.GetFullPath(executable);hash=Digest(image);}
            catch(Exception error) when(error is IOException or UnauthorizedAccessException){throw new BackendError("package_invalid","Backend executable unavailable");}
        }
        internal void CleanupCancel() {
            if(!cancelDelivered)return;
            try {
                string target=Path.Combine(root,"State","cancel-"+id);
                if(File.Exists(target)&&(File.GetAttributes(target)&FileAttributes.ReparsePoint)==0&&new FileInfo(target).Length==6&&File.ReadAllText(target)=="cancel")File.Delete(target);
            }
            catch(Exception error) when(error is IOException or UnauthorizedAccessException){}
        }
        internal void Own(Process value) {
            process=value;_=value.Handle;pid=value.Id;started=value.StartTime.ToUniversalTime();
            if(!Same(ProcessImage(value),image))throw new BackendError("package_invalid","Backend process image differs");
        }
        internal bool StopOwned() {
            lock(gate) {
            if(!canTerminate||critical||live)return false;
            try {
                if(process==null||process.HasExited)return true;
                if(process.Id!=pid||process.StartTime.ToUniversalTime()!=started||!Same(ProcessImage(process),image)||Digest(image!)!=hash)return false;
                process.Kill(entireProcessTree:false);return true;
            }
            catch(Exception error) when(error is IOException or UnauthorizedAccessException or InvalidOperationException or System.ComponentModel.Win32Exception){return process?.HasExited==true;}
            }
        }
        private static bool Same(string? a,string? b)=>string.Equals(a,b,OperatingSystem.IsWindows()?StringComparison.OrdinalIgnoreCase:StringComparison.Ordinal);
        private static string? ProcessImage(Process value) {
            if(OperatingSystem.IsLinux())return File.ResolveLinkTarget($"/proc/{value.Id}/exe",true)?.FullName;
            if(!OperatingSystem.IsWindows())return value.MainModule?.FileName;
            var path=new StringBuilder(32768);int size=path.Capacity;
            if(!QueryFullProcessImageName(value.Handle,0,path,ref size))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            return path.ToString();
        }
        [DllImport("kernel32.dll",EntryPoint="QueryFullProcessImageNameW",CharSet=CharSet.Unicode,SetLastError=true)]
        [return:MarshalAs(UnmanagedType.Bool)]
        private static extern bool QueryFullProcessImageName(IntPtr process,int flags,StringBuilder path,ref int size);
        private static string Digest(string path){using var file=File.OpenRead(path);return Convert.ToHexString(SHA256.HashData(file));}
    }
}
