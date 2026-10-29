using System.Diagnostics;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Security.Principal;
using System.Text.Json;
using Microsoft.Win32;
using System.Diagnostics.Eventing.Reader;
using System.Text.RegularExpressions;
using System.Xml.Linq;

// Opt-in real firewall/Wintun fixture. No game, installer, user profile or security-policy writes.
internal static class Program
{
    private static readonly List<string> Checks=[];
    private static int Main(string[] args)
    {
        if(args.Length!=3)return 2;
        string root=Path.GetFullPath(args[0]),helper=Path.GetFullPath(args[1]),runtime=Path.GetFullPath(args[2]);
        string allowed=Path.GetFullPath(Path.Combine(AppContext.BaseDirectory,"../test-temp"))+Path.DirectorySeparatorChar;
        if(!root.StartsWith(allowed,StringComparison.OrdinalIgnoreCase)||!Path.GetFileName(root).StartsWith("windows-api-live-",StringComparison.Ordinal)||
           Path.GetFileName(helper)!="WePlay.WindowsHelper.exe"||!Directory.Exists(root))return 2;
        if(!new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator))
        {
            try
            {
                var start=new ProcessStartInfo(Environment.ProcessPath!){UseShellExecute=true,Verb="runas",WindowStyle=ProcessWindowStyle.Hidden};
                foreach(var a in args)start.ArgumentList.Add(a);
                using var elevated=Process.Start(start)!;elevated.WaitForExit();return elevated.ExitCode;
            }
            catch(Exception){Save(root,false,"Administrator consent unavailable");return 2;}
        }
        var before=Policies();Process? core=null;bool lan=false,internet=false;
        string key=Guid.NewGuid().ToString("N"),name="WePlay-"+key[..8];
        try
        {
            Environment.SetEnvironmentVariable("PSExecutionPolicyPreference","Restricted",EnvironmentVariableTarget.Process);
            foreach(string relative in new[]{"Base/Bin/Settlers7R.exe","Runtime/dotnet/dotnet.exe","Runtime/python/python.exe"})
            {string path=Path.Combine(root,relative);Directory.CreateDirectory(Path.GetDirectoryName(path)!);File.Copy(Path.Combine(Environment.SystemDirectory,"cmd.exe"),path);}
            string et=Path.Combine(root,"Runtime/easytier");Directory.CreateDirectory(et);
            foreach(string file in new[]{"easytier-core.exe","wintun.dll"})File.Copy(Path.Combine(runtime,file),Path.Combine(et,file));
            File.WriteAllText(Path.Combine(root,"compatibility.json"),"{\"format\":\"s7-content-v1\"}");
            ReapPriorFixtures(allowed,root,helper);
            // Reap only inactive Wintun fixtures from this test's reserved 10.253/16.
            // Normal room creation uses second octets 32..223, never this test range.
            foreach(var stale in NetworkInterface.GetAllNetworkInterfaces().Where(x=>x.Name.StartsWith("WePlay-",StringComparison.Ordinal)&&x.OperationalStatus!=OperationalStatus.Up))
            {
                var addresses=stale.GetIPProperties().UnicastAddresses.Where(x=>x.Address.ToString().StartsWith("10.253.",StringComparison.Ordinal)).ToArray();
                if(addresses.Length==1)Call(helper,"adapter-cleanup",root,stale.Name,addresses[0].Address.ToString());
            }
            var plan=JsonDocument.Parse(Call(helper,"plan-lan",root,"apply"));string prefix=plan.RootElement[0].GetProperty("Name").GetString()![..23];
            // Mark before applying so finally also cleans up a partially failed call.
            lan=true;Call(helper,"lan-firewall",root,"apply");
            using(var read=JsonDocument.Parse(Call(helper,"inspect-firewall",prefix)))
            {
                Require(read.RootElement.GetArrayLength()==6,"six LAN rules");
                foreach(var rule in read.RootElement.EnumerateArray())
                    Require(rule.GetProperty("profiles").GetInt32()==2&&rule.GetProperty("remote").GetString()!.Equals("LocalSubnet",StringComparison.OrdinalIgnoreCase),"LAN scope");
            }
            Checks.Add("six real LAN rules restricted to Private/LocalSubnet");
            Call(helper,"lan-firewall",root,"remove");lan=false;
            using(var read=JsonDocument.Parse(Call(helper,"inspect-firewall",prefix)))Require(read.RootElement.GetArrayLength()==0,"LAN removal");
            Checks.Add("LAN rules and marker removed");

            // Unique fixture subnet; do not overlap an existing IPv4 interface.
            string subnet="10.253."+Random.Shared.Next(16,240),address=subnet+".1";
            Require(!NetworkInterface.GetAllNetworkInterfaces().SelectMany(x=>x.GetIPProperties().UnicastAddresses).Any(x=>x.Address.ToString().StartsWith(subnet+".",StringComparison.Ordinal)),"fixture subnet unused");
            int port=FreePort(),rpc=FreePort();
            string config=Path.Combine(root,"test-network.toml");
            File.WriteAllText(Path.Combine(root,"owned-adapter.json"),JsonSerializer.Serialize(new {name,address}));
            File.WriteAllText(config,$"""
instance_id = "{Guid.NewGuid()}"
instance_name = "{name}"
hostname = "{name}"
ipv4 = "{address}/24"
dhcp = false
listeners = ["tcp://127.0.0.1:{port}"]
stun_servers = []
stun_servers_v6 = []
[network_identity]
network_name = "weplay-fixture-{key}"
network_secret = "{Guid.NewGuid():N}{Guid.NewGuid():N}"
[flags]
dev_name = "{name}"
no_tun = false
enable_exit_node = false
accept_dns = false
private_mode = true
p2p_only = true
disable_upnp = true
relay_network_whitelist = ""
enable_encryption = true
""");
            var startCore=new ProcessStartInfo(Path.Combine(et,"easytier-core.exe")){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=et,RedirectStandardOutput=true,RedirectStandardError=true};
            foreach(string argument in new[]{"-c",config,"--disable-env-parsing","--rpc-portal","127.0.0.1:"+rpc,"--rpc-portal-whitelist","127.0.0.1/32","--console-log-level","off","--file-log-level","off"})startCore.ArgumentList.Add(argument);
            core=Process.Start(startCore)!;_=core.StandardOutput.ReadToEndAsync();_=core.StandardError.ReadToEndAsync();
            Until(()=>NetworkInterface.GetAllNetworkInterfaces().Any(x=>x.Name==name&&x.GetIPProperties().UnicastAddresses.Any(a=>a.Address.ToString()==address)),core);
            internet=true;Call(helper,"internet-firewall",root,key,"apply",address,name);
            using(var read=JsonDocument.Parse(Call(helper,"inspect-firewall","S7ET-"+key+"-")))
            {
                Require(read.RootElement.GetArrayLength()==8,"eight Internet rules");
                foreach(var rule in read.RootElement.EnumerateArray().Where(x=>!x.GetProperty("name").GetString()!.Contains("-core-")))
                {
                    Require(rule.GetProperty("local").GetString()!.StartsWith(address,StringComparison.Ordinal),"overlay local address");
                    Require(rule.GetProperty("remote").GetString()!.StartsWith(subnet+".0/",StringComparison.Ordinal),"overlay subnet");
                    Require(rule.GetProperty("interfaces").ToString().Contains(name,StringComparison.Ordinal),"overlay interface");
                }
            }
            Checks.Add("eight real Internet rules with adapter/address/subnet scope");
            Call(helper,"internet-firewall",root,key,"remove");internet=false;
            using(var read=JsonDocument.Parse(Call(helper,"inspect-firewall","S7ET-"+key+"-")))Require(read.RootElement.GetArrayLength()==0,"Internet removal");
            Checks.Add("Internet rules removed");
            core.Kill(true);core.WaitForExit(15000);core.Dispose();core=null;
            Call(helper,"adapter-cleanup",root,name,address);
            Until(()=>!NetworkInterface.GetAllNetworkInterfaces().Any(x=>x.Name==name),null);
            Checks.Add("owned Wintun adapter removed");
            Require(before.SequenceEqual(Policies()),"security policies unchanged");Checks.Add("PowerShell registry policies unchanged");
            Save(root,true,"");return 0;
        }
        catch(Exception error){Save(root,false,error.Message);return 2;}
        finally
        {
            if(internet){try{Call(helper,"internet-firewall",root,key,"remove");}catch(Exception){}}
            if(lan){try{Call(helper,"lan-firewall",root,"remove");}catch(Exception){}}
            if(core!=null){try{if(!core.HasExited){core.Kill(true);core.WaitForExit(15000);}}finally{core.Dispose();}}
            string owned=Path.Combine(root,"owned-adapter.json");
            if(File.Exists(owned))
            {
                using var identity=JsonDocument.Parse(File.ReadAllText(owned));
                try{Call(helper,"adapter-cleanup",root,identity.RootElement.GetProperty("name").GetString()!,identity.RootElement.GetProperty("address").GetString()!);}catch(Exception){}
            }
            File.Delete(Path.Combine(root,"test-network.toml"));
        }
    }
    private static string Call(string helper,params string[] args)
    {
        var start=new ProcessStartInfo(helper){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
        foreach(var arg in args)start.ArgumentList.Add(arg);
        using var process=Process.Start(start)!;var output=process.StandardOutput.ReadToEndAsync();var error=process.StandardError.ReadToEndAsync();
        if(!process.WaitForExit(30000))throw new Exception("owned helper timed out");
        string result=output.GetAwaiter().GetResult();if(process.ExitCode!=0)throw new Exception("owned helper failed: "+error.GetAwaiter().GetResult().Trim());
        return result;
    }
    private static void Until(Func<bool> ready,Process? process)
    {for(int i=0;i<150;i++){if(ready())return;if(process!=null&&process.HasExited)throw new Exception("owned core exited");Thread.Sleep(200);}throw new Exception("owned adapter timeout");}
    private static int FreePort(){var server=new TcpListener(IPAddress.Loopback,0);server.Start();int port=((IPEndPoint)server.LocalEndpoint).Port;server.Stop();return port;}
    private static void Require(bool condition,string reason){if(!condition)throw new Exception(reason);}
    private static object?[] Policies()=>[
        Registry.GetValue("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\PowerShell\\1\\ShellIds\\Microsoft.PowerShell","ExecutionPolicy",null),
        Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\PowerShell\\1\\ShellIds\\Microsoft.PowerShell","ExecutionPolicy",null),
        Registry.GetValue("HKEY_CURRENT_USER\\SOFTWARE\\Policies\\Microsoft\\Windows\\PowerShell","ExecutionPolicy",null),
        Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Policies\\Microsoft\\Windows\\PowerShell","ExecutionPolicy",null)];
    private static void Save(string root,bool passed,string reason)=>File.WriteAllText(Path.Combine(root,"live-result.json"),JsonSerializer.Serialize(new {passed,checks=Checks,reason}));

    private static void ReapPriorFixtures(string allowed,string current,string helper)
    {
        var candidates=new Dictionary<string,(string Root,string Address)>(StringComparer.Ordinal);
        var prior=Directory.GetDirectories(allowed,"windows-api-live-*").Where(x=>!x.Equals(current,StringComparison.OrdinalIgnoreCase)).Take(64).ToArray();
        foreach(string folder in prior)
        {
            string owned=Path.Combine(folder,"owned-adapter.json");if(!File.Exists(owned))continue;
            using var record=JsonDocument.Parse(File.ReadAllText(owned));
            candidates[record.RootElement.GetProperty("name").GetString()!]=(folder,record.RootElement.GetProperty("address").GetString()!);
        }
        // Early fixture revisions did not persist the alias before testing cleanup.
        // Recover only aliases tied to an exact private fixture executable in its
        // firewall-add event. Never infer ownership from a general WePlay prefix.
        const string channel="Microsoft-Windows-Windows Firewall With Advanced Security/Firewall";
        try
        {
            using var reader=new EventLogReader(new EventLogQuery(channel,PathType.LogName,"*[System[(EventID=2004)]]"){ReverseDirection=true});
            for(int i=0;i<512;i++)
            {
                using var item=reader.ReadEvent();if(item==null)break;
                var fields=XDocument.Parse(item.ToXml()).Descendants().Where(x=>x.Name.LocalName=="Data").Select(x=>x.Value).ToArray();
                string? folder=prior.FirstOrDefault(path=>fields.Any(value=>value.Contains(Path.GetFileName(path)+"\\",StringComparison.OrdinalIgnoreCase)));
                if(folder==null)continue;
                foreach(string field in fields)
                {
                    var match=Regex.Match(field,"\\AS7ET-([0-9a-f]{32})-");
                    if(!match.Success)continue;
                    string alias="WePlay-"+match.Groups[1].Value[..8];
                    var adapter=NetworkInterface.GetAllNetworkInterfaces().SingleOrDefault(x=>x.Name==alias);
                    if(adapter==null||adapter.OperationalStatus==OperationalStatus.Up)continue;
                    var addresses=adapter.GetIPProperties().UnicastAddresses.Where(x=>x.Address.AddressFamily==AddressFamily.InterNetwork).ToArray();
                    if(addresses.Length==0)candidates.TryAdd(alias,(folder,"10.253.0.1")); // No assigned address; ownership is the exact event path/key.
                    else if(addresses.Length==1&&addresses[0].Address.ToString().StartsWith("10.253.",StringComparison.Ordinal))
                        candidates.TryAdd(alias,(folder,addresses[0].Address.ToString()));
                }
            }
        }
        catch(EventLogNotFoundException){}
        foreach(var candidate in candidates)
            Call(helper,"adapter-cleanup",candidate.Value.Root,candidate.Key,candidate.Value.Address);
    }
}
