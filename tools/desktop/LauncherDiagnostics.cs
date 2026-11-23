using System.ComponentModel;
using System.Diagnostics;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace S7Desktop;

internal static class LauncherDiagnostics
{
    private const int EntryLimit=32,FileLimit=64*1024;
    private static readonly object Gate=new();
    private static readonly Dictionary<string,History> Histories=new(StringComparer.OrdinalIgnoreCase);
    private static readonly JsonSerializerOptions Format=new() {
        PropertyNamingPolicy=JsonNamingPolicy.SnakeCaseLower,MaxDepth=12
    };
    private static readonly HashSet<string> Actions= new(StringComparer.Ordinal) {
        "snapshot","detect","install","repair","save","play","lan-list","lan-play","lan-port",
        "firewall","probe","verify","diagnostics","update-check","update-install",
        "internet-create","internet-join","crossplay-create","crossplay-join",
        "directory-list","directory-connect","directory-join","discord-connect","discord-disconnect",
        "cleanup","cleanup-automatic","uninstall","reset-installation","recovery-status",
        "recovery-resolve","migrate-record","launcher","unknown"
    };
    private static readonly HashSet<string> Components= new(StringComparer.Ordinal) {
        "desktop_backend","desktop_settings","install","standalone","launch_local","launch_lan",
        "launch_auto_lan","launch_internet","lan_autoconnect","lan_control","lan_discovery","lan_hub",
        "lan_interfaces","lan_child_job","lobby_directory","crossplay_network","crossplay_session",
        "crossplay_runtime","crossplay_room","crossplay_content","crossplay_invitation","easytier_elevation",
        "easytier_runtime","internet_room","windows_system","network_elevation","network_peer_policy",
        "isolated_process","platform_standalone","host_runtime","graphics_profile","graphics_choice",
        "quality_settings","gold_graphics","reshade_runtime","display_settings","compatibility",
        "update_client","game_directory","memory_guard","release_policy","gold_support"
    };
    private static readonly string[] ReportFields=[
        "release","source_commit","release_metadata_network_revision","content_contract","directory_protocol_revision",
        "neo_wire_revision","game_bits","renderer","graphics_devices","probe_status","last_lan_start","lan_discovery_port","memory_summary",
        "collection_status","known_lan_desync_resolved"
    ];
    private static readonly Lazy<HashSet<string>> Codes=new(() => {
        using var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("S7Launcher.Strings.en.json");
        return stream is null?new HashSet<string>(StringComparer.Ordinal):
            new HashSet<string>(JsonSerializer.Deserialize<Dictionary<string,string>>(stream)!.Keys,StringComparer.Ordinal);
    });
    private static string? Code(string? value)=>value is not null&&Codes.Value.Contains(value)?value:null;
    private static string? Phase(string? value)=>value is not null&&Code("phase_"+value)!=null?value:null;
    private static string? Kind(string? value)=>value is "os_error" or "timeout" or "http_error" or "invalid_input"
        or "runtime_error" or "unexpected" or "cancelled" or "backend_error"?value:null;
    private static string? Stage(string? value)=>value is "request" or "process_start" or "ipc" or "process_exit" or "ui"?value:null;
    private static string? Status(string? value)=>value is "cancellable" or "critical" or "live_game"?value:null;
    private static string? String(JsonElement item,string name)=>
        item.ValueKind==JsonValueKind.Object&&item.TryGetProperty(name,out var value)&&value.ValueKind==JsonValueKind.String
            ?value.GetString():null;
    private static int? Number(JsonElement item,string name)=>
        item.ValueKind==JsonValueKind.Object&&item.TryGetProperty(name,out var value)&&value.ValueKind==JsonValueKind.Number
            &&value.TryGetInt32(out int number)?number:null;

    private static uint? UnsignedNumber(JsonElement item,string name)=>
        item.ValueKind==JsonValueKind.Object&&item.TryGetProperty(name,out var value)&&value.ValueKind==JsonValueKind.Number
            &&value.TryGetUInt32(out uint number)?number:null;

    internal sealed record Fault(string? Kind,string? Component,int? Line,int? SystemCode,
        int? NativeStartStage=null,uint? NativeExitCode=null,int? WinsockCode=null)
    {
        internal Fault Safe()=>new(LauncherDiagnostics.Kind(Kind),
            Component is not null&&Components.Contains(Component)?Component:null,Line is >0 and <100000?Line:null,SystemCode,
            NativeStartStage is >0 and <=255?NativeStartStage:null,NativeExitCode,WinsockCode is >0 and <=65535?WinsockCode:null);
    }
    internal sealed record Entry(
        DateTimeOffset StartedUtc,string Action,string Outcome,string? Stage,string? LastPhase,string[] Phases,string? LastActiveState,
        string? Code,Fault? Fault,int? BackendExitCode,bool GameStarted,int? GameExitCode,string[] Warnings,long DurationMs)
    {
        internal Entry Safe()=>new(StartedUtc,Actions.Contains(Action)?Action:"unknown",
            Outcome is "ok" or "failed" or "cancelled"?Outcome:"failed",LauncherDiagnostics.Stage(Stage),Phase(LastPhase),
            (Phases??[]).Select(Phase).OfType<string>().TakeLast(12).ToArray(),Status(LastActiveState),LauncherDiagnostics.Code(Code),Fault?.Safe(),
            BackendExitCode,GameStarted,GameExitCode,(Warnings??[]).Select(LauncherDiagnostics.Code).OfType<string>().TakeLast(6).ToArray(),
            Math.Clamp(DurationMs,0,7*24*60*60*1000L));
    }

    internal sealed class Operation(string action)
    {
        private readonly DateTimeOffset started=DateTimeOffset.UtcNow;
        private readonly Stopwatch clock=Stopwatch.StartNew();
        private readonly List<string> phases=[],warnings=[];
        private Fault? fault;
        private string? lastPhase,lastActiveState,code;
        private bool gameStarted;
        private int? gameExit;
        internal string CurrentStage {get;set;}="request";
        internal int? ExitCode {get;set;}
        internal void Warn(string? warning) {
            if(Code(warning) is not string known)return;
            lock(warnings){if(!warnings.Contains(known)&&warnings.Count<6)warnings.Add(known);}
        }
        internal void Observe(JsonElement item,string? kind) {
            switch(kind) {
                case "progress":
                    if(Phase(String(item,"phase")) is string phase&&phase!=lastPhase) {
                        lastPhase=phase;
                        if(phases.Count==12)phases.RemoveAt(0);
                        phases.Add(phase);
                    }
                    break;
                case "warning": Warn(String(item,"code"));break;
                case "operation_state": lastActiveState=Status(String(item,"phase"))??lastActiveState;break;
                case "game_started":gameStarted=true;break;
                case "game_finished":gameExit=Number(item,"exit_code");break;
                case "result":
                    if(item.TryGetProperty("data",out var result)&&result.ValueKind==JsonValueKind.Object&&
                        result.TryGetProperty("network_setup_failures",out var failures)&&failures.ValueKind==JsonValueKind.Array)
                        foreach(var warning in failures.EnumerateArray().Take(6))
                            if(warning.ValueKind==JsonValueKind.String)Warn(warning.GetString());
                    break;
                case "error":
                    if(item.TryGetProperty("diagnostic",out var data))
                        fault=new Fault(String(data,"kind"),String(data,"component"),Number(data,"line"),Number(data,"system_code"),
                            Number(data,"native_start_stage"),UnsignedNumber(data,"native_exit_code"),Number(data,"winsock_code")).Safe();
                    break;
            }
        }
        internal void Fail(Exception error) {
            code=error is BackendError backend?Code(backend.Code)??"operation_failed":
                error is OperationCanceledException?"cancelled":"operation_failed";
            CaptureFault(error);
        }
        internal void CaptureFault(Exception error)=>fault??=ExceptionFault(error);
        internal Entry Finish()=>new(started,action,code switch{null=>"ok","cancelled"=>"cancelled",_=>"failed"},
            CurrentStage,lastPhase,phases.ToArray(),lastActiveState,code,fault,ExitCode,gameStarted,gameExit,warnings.ToArray(),clock.ElapsedMilliseconds);
    }

    private sealed record History(List<Entry> Entries,bool Persisted);

    internal static Fault ExceptionFault(Exception error)=>error switch {
        Win32Exception windows=>new("os_error",null,null,windows.NativeErrorCode),
        IOException=>new("os_error",null,null,error.HResult),
        UnauthorizedAccessException=>new("os_error",null,null,error.HResult),
        OperationCanceledException=>new("cancelled",null,null,null),
        TimeoutException=>new("timeout",null,null,null),
        BackendError=>new("backend_error",null,null,null),
        _=>new("unexpected",null,null,null)
    };

    private static string StateFolder(string root,bool create)
    {
        string state=Path.Combine(root,"State");
        for(DirectoryInfo? parent=new(root);parent!=null;parent=parent.Parent)
            if((parent.Attributes&FileAttributes.ReparsePoint)!=0)throw new IOException();
        if(create)Directory.CreateDirectory(state);
        if((File.GetAttributes(state)&FileAttributes.ReparsePoint)!=0)throw new IOException();
        return state;
    }

    private static History Read(string root)
    {
        try {
            string path=Path.Combine(StateFolder(root,false),"launcher-history.json");
            if((File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)return new([],false);
            using var file=File.OpenRead(path);
            if(file.Length>FileLimit)return new([],false);
            byte[] data=new byte[file.Length];
            file.ReadExactly(data);
            var entries=JsonSerializer.Deserialize<Entry[]>(data,Format)??[];
            return new(entries.TakeLast(EntryLimit).Where(entry=>entry!=null).Select(entry=>entry.Safe()).ToList(),true);
        }
        catch(Exception error) when(error is IOException or UnauthorizedAccessException or JsonException) {return new([],false);}
    }

    internal static void Record(string root,Entry entry)
    {
        if(entry.Action=="diagnostics"||(entry.Outcome=="ok"&&entry.Action is
            ("snapshot" or "directory-list" or "update-check" or "recovery-status")))return;
        lock(Gate) {
            var history=Histories.GetValueOrDefault(root)??Read(root);
            history.Entries.Add(entry.Safe());
            if(history.Entries.Count>EntryLimit)history.Entries.RemoveAt(0);
            string? temporary=null;bool persisted=false;
            try {
                string target=Path.Combine(StateFolder(root,true),"launcher-history.json");
                if(File.Exists(target)&&(File.GetAttributes(target)&FileAttributes.ReparsePoint)!=0)throw new IOException();
                temporary=target+"."+Guid.NewGuid().ToString("N")+".tmp";
                using(var file=new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.None))
                    JsonSerializer.Serialize(file,history.Entries,Format);
                File.Move(temporary,target,true);
                persisted=true;
            }
            catch(Exception error) when(error is IOException or UnauthorizedAccessException) {}
            finally {
                if(temporary is not null)
                    try{File.Delete(temporary);}catch(Exception error) when(error is IOException or UnauthorizedAccessException){}
            }
            Histories[root]=history with {Persisted=persisted};
        }
    }

    internal static void RecordUiError(string root,Exception error)
    {
        if(error.Data.Contains("weplay_operation_recorded"))return;
        var operation=new Operation("launcher"){CurrentStage="ui"};
        operation.Fail(error);
        Record(root,operation.Finish());
    }

    internal static JsonObject Export(string root,string launcherVersion,JsonElement backend,Exception? backendFailure)
    {
        var result=new JsonObject();
        if(backend.ValueKind==JsonValueKind.Object) {
            foreach(string name in ReportFields)
                if(backend.TryGetProperty(name,out var value))result[name]=JsonNode.Parse(value.GetRawText());
            if(backend.TryGetProperty("recovery",out var recovery)&&recovery.ValueKind==JsonValueKind.Object) {
                var safeRecovery=new JsonObject();
                foreach(string name in new[]{"pending","recoverable","can_detach","conflicts"})
                    if(recovery.TryGetProperty(name,out var value))safeRecovery[name]=JsonNode.Parse(value.GetRawText());
                result["recovery"]=safeRecovery;
            }
        }
        result["format"]="s7-desktop-diagnostics-v1";
        if(!result.ContainsKey("release"))result["release"]=launcherVersion;
        result["launcher_version"]=launcherVersion;
        result["generated_utc"]=DateTimeOffset.UtcNow;
        if(backendFailure!=null)
        {
            result["backend_status"]="unavailable";
            result["backend_export_error"]=backendFailure is BackendError error?Code(error.Code)??"operation_failed":"operation_failed";
            result["backend_export_fault"]=JsonSerializer.SerializeToNode(ExceptionFault(backendFailure),Format);
        }
        lock(Gate) {
            var history=Histories.GetValueOrDefault(root)??Read(root);
            result["launcher_history_storage"]=history.Persisted?"persistent":history.Entries.Count>0?"memory":"empty";
            result["launcher_history"]=JsonSerializer.SerializeToNode(history.Entries,Format);
        }
        return result;
    }
}
