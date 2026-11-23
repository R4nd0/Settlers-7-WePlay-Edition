using System.Diagnostics;
using System.Reflection;
using System.Text;
using System.Text.Json;
using S7Desktop;

namespace S7Desktop {
    internal sealed class BackendError(string code,string detail):Exception(detail){public string Code=>code;}
    internal static class Program {public static string AppRoot=>AppContext.BaseDirectory;}
}

internal static class Tests
{
    // Allow cold .NET startup on loaded CI hosts; these are lifecycle tests,
    // not sub-second latency benchmarks. Protected bodies outlast Idle+Grace.
    private static readonly BackendTiming FaultTiming=new(TimeSpan.FromSeconds(2),TimeSpan.FromMilliseconds(500),TimeSpan.FromMilliseconds(50));
    private static int checks;
    private static async Task<int> Main(string[] args) {
        if(args.Length>0&&args[0]=="--child")return await Child(args);
        if(args.Length>0&&args[0]=="--sentinel") {
            var waiting=Stopwatch.StartNew();
            while(!File.Exists(args[1]+".release")&&waiting.Elapsed<TimeSpan.FromSeconds(20))await Task.Delay(50);
            if(!File.Exists(args[1]+".release"))return 4;
            File.WriteAllText(args[1],"descendant survived");return 0;
        }
        string basePath=Environment.GetEnvironmentVariable("S7_TEST_TEMP_ROOT")??Path.Combine(Path.GetTempPath(),"weplay-backend-tests");Directory.CreateDirectory(basePath);
        string root=Path.Combine(basePath,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        try {
            await Scenario(root,"normal","directory-list",null,null);
            await Scenario(root,"hang","directory-list","backend_timeout",null);
            await Scenario(root,"normal","recovery-status",null,null);
            await Scenario(root,"hang","recovery-status","backend_timeout",null);
            using(var stop=new CancellationTokenSource())await Scenario(root,"hang","directory-list","cancelled",stop);
            await Scenario(root,"oversize","directory-list","backend_protocol_invalid",null);
            await Scenario(root,"malformed","directory-list","backend_protocol_invalid",null);
            await Scenario(root,"cooperative","install",null,null);
            await Scenario(root,"critical","install",null,null);
            await Scenario(root,"critical-read","directory-list",null,null);
            await Scenario(root,"live","play",null,null);
            await Scenario(root,"live-read","directory-list",null,null);
            await Scenario(root,"mutating","save",null,null);
            await Scenario(root,"diagnostic-lan","lan-play","operation_failed",null);
            await Scenario(root,"diagnostic-native","internet-create","operation_failed",null);
            await Scenario(root,"diagnostic-native","lan-play","operation_failed",null);
            await Scenario(root,"failed-cancel","directory-list","backend_timeout",null);
            await Scenario(root,"descendant","directory-list","backend_timeout",null);
            var missing=new ProcessStartInfo(Path.Combine(root,"missing-child")){RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true};
            await Expect("package_invalid",()=>Backend.RunProcess(missing,root,Guid.NewGuid().ToString("N"),"directory-list",new{},_=>{},true,default,FaultTiming));
            Check(Backend.Operation==null,"startup failure clears operation");
            using(var snapshot=JsonDocument.Parse(LauncherDiagnostics.Export(root,"fixture",default,new BackendError("package_invalid","private-path")).ToJsonString()))
            {
                var report=snapshot.RootElement;
                Check(report.GetProperty("backend_status").GetString()=="unavailable","export works without backend");
                var entry=report.GetProperty("launcher_history").EnumerateArray().Last();
                Check(entry.GetProperty("stage").GetString()=="process_start","reports process startup failure stage");
                Check(entry.GetProperty("fault").GetProperty("system_code").ValueKind==JsonValueKind.Number,"reports system startup error number");
                Check(!report.GetRawText().Contains("private-path"),"export drops exception messages");
            }
            using(var reader=new StreamReader(new MemoryStream(Encoding.UTF8.GetBytes(new string('x',4097)))))
                await Expect("backend_protocol_invalid",()=>Backend.ReadLineBounded(reader,4096,default));
            Console.WriteLine($"PASS: {checks} backend lifecycle checks; actual own child processes, bounded hangs/frames, protected commit/game phases.");return 0;
        }
        finally {Directory.Delete(root,true);}
    }

    private static async Task Scenario(string root,string mode,string action,string? error,CancellationTokenSource? cancellation) {
        string folder=Path.Combine(root,mode+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
        string id=Guid.NewGuid().ToString("N");
        if(mode=="failed-cancel")Directory.CreateDirectory(Path.Combine(folder,"State","cancel-"+id));
        var start=new ProcessStartInfo(Environment.ProcessPath!){UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true,StandardInputEncoding=new UTF8Encoding(false)};
        if(Path.GetFileNameWithoutExtension(Environment.ProcessPath!).Equals("dotnet",StringComparison.OrdinalIgnoreCase))start.ArgumentList.Add(Assembly.GetExecutingAssembly().Location);
        foreach(string arg in new[]{"--child",mode,folder,id})start.ArgumentList.Add(arg);
        int warnings=0;
        // The cancellation case is triggered by fixture readiness, never by
        // time elapsed while the runtime is still starting.
        var timing=cancellation==null?FaultTiming:FaultTiming with{Idle=TimeSpan.FromSeconds(30)};
        Task<JsonElement> run=Backend.RunProcess(start,folder,id,action,new{},e=>{if(e.GetProperty("kind").GetString()=="warning")warnings++;},true,cancellation?.Token??default,timing);
        if(cancellation!=null) {
            var waiting=Stopwatch.StartNew();
            while(!File.Exists(Path.Combine(folder,"ready"))&&!run.IsCompleted&&waiting.Elapsed<TimeSpan.FromSeconds(20))await Task.Delay(50);
            if(!File.Exists(Path.Combine(folder,"ready")))throw new Exception("cancellation fixture did not become ready");
            cancellation.Cancel();
        }
        if(error!=null)await Expect(error,()=>run.WaitAsync(TimeSpan.FromSeconds(20)));
        else {var result=await run.WaitAsync(TimeSpan.FromSeconds(20));Check(result.GetProperty("ok").GetBoolean(),mode+" result");}
        Check(Backend.Operation==null,mode+" operation cleared");
        var identity=JsonDocument.Parse(File.ReadAllText(Path.Combine(folder,"child.json"))).RootElement;
        int pid=identity.GetProperty("pid").GetInt32();
        bool alive=false;try{using var child=Process.GetProcessById(pid);alive=!child.HasExited&&child.StartTime.ToUniversalTime().Ticks==identity.GetProperty("started").GetInt64();}catch(ArgumentException){}
        Check(!alive,mode+" child exited");
        if(error!=null)Check(run.IsCompleted,mode+" bounded recovery");
        if(mode.StartsWith("critical")||mode is "mutating" or "failed-cancel")Check(warnings==1,mode+" warns once");
        if(mode.StartsWith("live"))Check(warnings==0,"live game has no false idle warning");
        if(mode.StartsWith("critical")||mode.StartsWith("live")||mode=="mutating")Check(File.Exists(Path.Combine(folder,"completed")),mode+" never force killed");
        if(mode=="descendant") {
            string marker=Path.Combine(folder,"descendant-completed");
            // Release only after the direct parent has exited, so a pre-kill
            // sentinel completion cannot produce a false positive.
            File.WriteAllText(marker+".release","parent exited");
            var deadline=Stopwatch.StartNew();while(!File.Exists(marker)&&deadline.Elapsed<TimeSpan.FromSeconds(10))await Task.Delay(50);
            Check(File.Exists(marker),"owned direct-child stop preserves descendant process");
        }
        if(mode=="diagnostic-lan")
        {
            string persisted=Path.Combine(folder,"State","launcher-history.json");
            string reopened=Path.Combine(root,"diagnostic-reopened");
            Directory.CreateDirectory(Path.Combine(reopened,"State"));
            File.Copy(persisted,Path.Combine(reopened,"State","launcher-history.json"));
            using var payload=JsonDocument.Parse("""{"release":"fixture","stderr":"private-path","recovery":{"pending":true,"token":"private-token"}}""");
            using var report=JsonDocument.Parse(LauncherDiagnostics.Export(reopened,"fixture",payload.RootElement,null).ToJsonString());
            var entry=report.RootElement.GetProperty("launcher_history").EnumerateArray().Single();
            Check(entry.GetProperty("action").GetString()=="lan-play","operation persisted and reads on a clean session");
            Check(entry.GetProperty("last_phase").GetString()=="lan_connect","records latest LAN phase");
            Check(entry.GetProperty("last_active_state").GetString()=="critical","records last non-idle backend state");
            Check(entry.GetProperty("fault").GetProperty("system_code").GetInt32()==10048,"socket error number is shareable");
            Check(entry.GetProperty("fault").GetProperty("winsock_code").GetInt32()==10013,"discovery Winsock code survives persistence and export");
            Check(entry.GetProperty("fault").GetProperty("component").GetString()=="lan_hub","component without local path");
            Check(entry.GetProperty("backend_exit_code").GetInt32()==2,"reports backend exit code");
            Check(entry.GetProperty("warnings").GetArrayLength()==1,"retains recognized warnings");
            Check(!report.RootElement.GetRawText().Contains("private-"),"drops input, raw logs, error text and recovery tokens");
        }
        if(mode=="diagnostic-native") {
            string reopened=folder+"-reopened";
            Directory.CreateDirectory(Path.Combine(reopened,"State"));
            File.Copy(Path.Combine(folder,"State","launcher-history.json"),Path.Combine(reopened,"State","launcher-history.json"));
            using var report=JsonDocument.Parse(LauncherDiagnostics.Export(reopened,"fixture",default,null).ToJsonString());
            var entry=report.RootElement.GetProperty("launcher_history").EnumerateArray().Single();
            var fault=entry.GetProperty("fault");
            Check(entry.GetProperty("action").GetString()==action,"socket probe failure is attached to the initiating action");
            Check(fault.GetProperty("native_start_stage").GetInt32()==11,"native startup stage survives persistence and export");
            Check(fault.GetProperty("native_exit_code").GetUInt32()==0xe143271d,"preserves unsigned native process exit code");
            Check(fault.GetProperty("network_probe").GetString()=="ipv4_nonloopback_udp","failed socket test survives persistence and export");
            Check(fault.GetProperty("network_probe_step").GetString()=="bind","failed socket operation survives persistence and export");
            Check(fault.GetProperty("winsock_code").GetInt32()==10013,"native socket error survives persistence and export");
            Check(!entry.GetProperty("game_started").GetBoolean(),"native initialization failure is recorded before game start");
        }
    }

    private static async Task<int> Child(string[] args) {
        string mode=args[1],root=args[2],id=args[3];
        File.WriteAllText(Path.Combine(root,"child.json"),JsonSerializer.Serialize(new{pid=Environment.ProcessId,started=Process.GetCurrentProcess().StartTime.ToUniversalTime().Ticks}));
        _=await Console.In.ReadToEndAsync();
        File.WriteAllText(Path.Combine(root,"ready"),"stdin consumed");
        if(mode=="diagnostic-lan") {
            Console.WriteLine("""{"kind":"progress","phase":"lan_search"}""");
            Console.WriteLine("""{"kind":"progress","phase":"lan_connect"}""");
            Console.WriteLine("""{"kind":"operation_state","phase":"critical"}""");
            Console.WriteLine("""{"kind":"log","text":"private-address 192.168.1.24"}""");
            Console.WriteLine("""{"kind":"warning","code":"backend_unresponsive"}""");
            Console.WriteLine("""{"kind":"operation_state","phase":"idle"}""");
            Console.WriteLine("""{"kind":"error","code":"operation_failed","detail":"private-invite and private-path","diagnostic":{"kind":"os_error","component":"lan_hub","line":55,"system_code":10048,"winsock_code":10013}}""");
            return 2;
        }
        if(mode=="diagnostic-native") {
            Console.WriteLine("""{"kind":"progress","phase":"starting"}""");
            Console.WriteLine("""{"kind":"error","code":"operation_failed","detail":"","diagnostic":{"kind":"runtime_error","component":"isolated_process","line":395,"native_start_stage":11,"native_exit_code":3779274525,"network_probe":"ipv4_nonloopback_udp","network_probe_step":"bind","winsock_code":10013}}""");
            return 2;
        }
        if(mode=="normal"){Result();return 0;}
        if(mode=="oversize"){Console.Write(new string('x',200000));Console.Out.Flush();}
        if(mode=="malformed"){Console.WriteLine("{broken");Console.Out.Flush();}
        if(mode=="descendant") {
            var sentinel=new ProcessStartInfo(Environment.ProcessPath!){UseShellExecute=false,CreateNoWindow=true};
            if(Path.GetFileNameWithoutExtension(Environment.ProcessPath!).Equals("dotnet",StringComparison.OrdinalIgnoreCase))sentinel.ArgumentList.Add(Assembly.GetExecutingAssembly().Location);
            sentinel.ArgumentList.Add("--sentinel");sentinel.ArgumentList.Add(Path.Combine(root,"descendant-completed"));
            using var child=Process.Start(sentinel)??throw new Exception("sentinel did not start");
        }
        if(mode is "hang" or "oversize" or "malformed" or "failed-cancel" or "descendant"){await Task.Delay(TimeSpan.FromSeconds(30));return 3;}
        if(mode=="cooperative") {
            Console.WriteLine("{\"kind\":\"operation_state\",\"phase\":\"cancellable\"}");Console.Out.Flush();
            while(!File.Exists(Path.Combine(root,"State","cancel-"+id)))await Task.Delay(20);
            Result();return 0;
        }
        if(mode.StartsWith("critical"))Console.WriteLine("{\"kind\":\"operation_state\",\"phase\":\"critical\"}");
        if(mode.StartsWith("live"))Console.WriteLine("{\"kind\":\"game_started\"}");
        Console.Out.Flush();await Task.Delay(FaultTiming.Idle+FaultTiming.Grace+TimeSpan.FromSeconds(1));File.WriteAllText(Path.Combine(root,"completed"),"safe completion");
        if(mode.StartsWith("live"))Console.WriteLine("{\"kind\":\"game_finished\"}");
        Console.WriteLine("{\"kind\":\"operation_state\",\"phase\":\"idle\"}");Result();return 0;
    }
    private static void Result(){Console.WriteLine("{\"kind\":\"result\",\"data\":{\"ok\":true}}");Console.Out.Flush();}
    private static void Check(bool valid,string reason){if(!valid)throw new Exception(reason);checks++;}
    private static async Task Expect(string code,Func<Task> run){try{await run();throw new Exception("Missing "+code);}catch(BackendError error){Check(error.Code==code,$"Expected {code}, got {error.Code}: {error.Message}");}}
}
