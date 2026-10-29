using System.Collections;
using System.Diagnostics;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using WePlay.Helpers;

internal sealed record Rule(string Name,string Program,int Protocol,int Profiles,string Remote,string Local="*",string? Interface=null,string? Port=null);

internal static class Program
{
    private static readonly string[] Programs=["Base/Bin/Settlers7R.exe","Runtime/dotnet/dotnet.exe","Runtime/python/python.exe"];
    private const string Group="WePlay managed networking";

    [STAThread]
    private static int Main(string[] args)
    {
        Console.OutputEncoding=new UTF8Encoding(false);
        try
        {
            if(args is ["interfaces"]){Print(Interfaces());return 0;}
            if(args is ["processes"]){Print(Processes());return 0;}
            if(args is ["inspect-firewall",string prefix]){Print(Inspect(prefix));return 0;}
            if(args is ["adapter-cleanup",string bundle,string adapter,string address])
            {
                Bundle(bundle);if(!IsAdmin())throw new UnauthorizedAccessException("helper_admin_required");
                AdapterCleanup.Remove(adapter,address);Print(new {ok=true});return 0;
            }
            bool plan=args.Length>0&&args[0].StartsWith("plan-",StringComparison.Ordinal);
            if(args.Length>0&&args[0] is "lan-firewall" or "plan-lan")
            {
                if(args.Length!=3||args[2] is not ("apply" or "remove"))throw new InvalidDataException("helper_invalid");
                string root=Bundle(args[1]);var rules=LanRules(root);
                if(plan){Print(rules);return 0;}
                if(!IsAdmin())return Elevate(args);
                Apply(rules,args[2]=="remove");
                string marker=SafePaths.Plain(Path.Combine(root,"LocalData/desktop-firewall.json"));
                if(args[2]=="remove")File.Delete(marker);
                else
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(marker)!);
                    WriteAtomic(marker,JsonSerializer.Serialize(new {format="s7-private-firewall-v1",key=LanKey(root)}));
                }
                Print(new {ok=true});return 0;
            }
            if(args.Length>0&&args[0] is "internet-firewall" or "plan-internet")
            {
                if(args.Length is not (4 or 6)||!Regex.IsMatch(args[2],"\\A[0-9a-f]{32}\\z"))throw new InvalidDataException("helper_invalid");
                string root=Bundle(args[1]);bool remove=args[3]=="remove";
                if((remove&&args.Length!=4)||(!remove&&(args[3]!="apply"||args.Length!=6)))throw new InvalidDataException("helper_invalid");
                // Removal needs only owned names; no live adapter is required after failure.
                var rules=InternetRules(root,args[2],remove?null:args[4],remove?null:args[5]);
                if(plan){Print(rules);return 0;}
                if(!IsAdmin())throw new UnauthorizedAccessException("helper_admin_required");
                Apply(rules,remove);Print(new {ok=true});return 0;
            }
            throw new InvalidDataException("helper_invalid");
        }
        catch(Exception error)
        {
            string code=error.Message;
            if(!Regex.IsMatch(code,"\\Ahelper_[a-z_]+\\z"))code=error is UnauthorizedAccessException?"helper_permission_denied":"helper_failed";
            Console.Error.WriteLine(code);
#if HELPER_DIAGNOSTICS
            Console.Error.WriteLine(error);
#endif
            return 2;
        }
    }
    private static void Print(object value)=>Console.WriteLine(JsonSerializer.Serialize(value));
    private static bool IsAdmin()=>new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
    private static int Elevate(string[] args)
    {
        // Do not pass runtime startup hooks/profilers to the elevated helper.
        // This changes only our process environment, never a user/machine setting.
        foreach(string key in Environment.GetEnvironmentVariables().Keys)
            if(new[]{"DOTNET_","CORECLR_","COR_","COMPLUS_"}.Any(prefix=>key.StartsWith(prefix,StringComparison.OrdinalIgnoreCase)))
                Environment.SetEnvironmentVariable(key,null,EnvironmentVariableTarget.Process);
        string self=SafePaths.Plain(Environment.ProcessPath??throw new InvalidDataException("helper_invalid"));
        var start=new ProcessStartInfo(self){UseShellExecute=true,Verb="runas",WindowStyle=ProcessWindowStyle.Hidden};
        foreach(string argument in args)start.ArgumentList.Add(argument);
        try{using var child=Process.Start(start)??throw new InvalidDataException("helper_failed");child.WaitForExit();return child.ExitCode;}
        catch(System.ComponentModel.Win32Exception error) when(error.NativeErrorCode==1223){throw new InvalidDataException("helper_cancelled");}
    }
    private static string Bundle(string path)
    {
        string root=SafePaths.Plain(path);
        string manifest=SafePaths.FileAt(root,"compatibility.json");
        if(new FileInfo(manifest).Length>4*1024*1024)throw new InvalidDataException("helper_invalid");
        using(var json=JsonDocument.Parse(File.ReadAllBytes(manifest)))
            if(json.RootElement.GetProperty("format").GetString()!="s7-content-v1")throw new InvalidDataException("helper_invalid");
        foreach(string program in Programs)SafePaths.FileAt(root,program);
        return root;
    }
    private static string LanKey(string root)=>Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(root.ToLowerInvariant())))[..16];
    private static List<Rule> LanRules(string root)
    {
        var result=new List<Rule>();string key=LanKey(root);
        for(int i=0;i<Programs.Length;i++)foreach(int protocol in new[]{6,17})
            result.Add(new($"S7LAN-{key}-{i}-{(protocol==6?"TCP":"UDP")}",SafePaths.FileAt(root,Programs[i]),protocol,2,"LocalSubnet"));
        return result;
    }
    private static List<Rule> InternetRules(string root,string key,string? address,string? name)
    {
        string local="*",remote="*";
        if(address!=null)
        {
            if(name==null||!Regex.IsMatch(name,"\\AWePlay-[0-9a-f]{8}\\z")||!IPAddress.TryParse(address,out var ip)||ip.AddressFamily!=AddressFamily.InterNetwork||ip.ToString()!=address)throw new InvalidDataException("helper_invalid");
            byte[] b=ip.GetAddressBytes();
            if(!(b[0]==10||b[0]==172&&b[1]>=16&&b[1]<=31||b[0]==192&&b[1]==168)||b[3] is 0 or 255)throw new InvalidDataException("helper_invalid");
            var adapters=NetworkInterface.GetAllNetworkInterfaces().Where(x=>x.Name==name&&x.OperationalStatus==OperationalStatus.Up).ToArray();
            if(adapters.Length!=1)throw new InvalidDataException("helper_overlay_changed");
            var addresses=adapters[0].GetIPProperties().UnicastAddresses.Where(x=>x.Address.AddressFamily==AddressFamily.InterNetwork).ToArray();
            if(addresses.Length!=1||addresses[0].Address.ToString()!=address||addresses[0].PrefixLength!=24)throw new InvalidDataException("helper_overlay_changed");
            local=address;remote=$"{b[0]}.{b[1]}.{b[2]}.0/24";
        }
        var result=new List<Rule>();
        for(int i=0;i<Programs.Length;i++)foreach(int protocol in new[]{6,17})
            result.Add(new($"S7ET-{key}-{i}-{(protocol==6?"TCP":"UDP")}",SafePaths.FileAt(root,Programs[i]),protocol,int.MaxValue,remote,local,name));
        string core=SafePaths.FileAt(root,"Runtime/easytier/easytier-core.exe");
        result.Add(new($"S7ET-{key}-core-TCP",core,6,int.MaxValue,"*",Port:"11010"));
        result.Add(new($"S7ET-{key}-core-UDP",core,17,int.MaxValue,"*"));
        return result;
    }
    private static dynamic Com(string progId)=>Activator.CreateInstance(Type.GetTypeFromProgID(progId,true)!)!;
    private static void Apply(List<Rule> plans,bool remove)
    {
        dynamic policy=Com("HNetCfg.FwPolicy2");dynamic rules=policy.Rules;
        var added=new List<string>();
        try
        {
            // Group policy can disallow locally added rules. Report failure instead of pretending success.
            if(!remove&&(int)policy.LocalPolicyModifyState!=0)throw new UnauthorizedAccessException("helper_policy_blocked");
            foreach(var plan in plans)
            {
                dynamic? existing=null;
                // The CLR maps this COM HRESULT to FileNotFoundException on some systems.
                try{existing=rules.Item(plan.Name);}catch(Exception error) when((uint)error.HResult==0x80070002){}
                if(existing!=null)
                {
                    try
                    {
                        if(!string.Equals((string)existing.ApplicationName,plan.Program,StringComparison.OrdinalIgnoreCase)||
                           (int)existing.Direction!=1||(int)existing.Action!=1)throw new InvalidDataException("helper_rule_conflict");
                        rules.Remove(plan.Name);
                    }
                    finally{Marshal.FinalReleaseComObject(existing);}
                }
                if(remove)continue;
                dynamic rule=Com("HNetCfg.FWRule");
                try
                {
                    rule.Name=plan.Name;rule.Description="Scoped rule owned by this WePlay installation/room.";
                    rule.Grouping=Group;rule.ApplicationName=plan.Program;rule.Protocol=plan.Protocol;
                    rule.Direction=1;rule.Action=1;rule.Profiles=plan.Profiles;rule.RemoteAddresses=plan.Remote;rule.LocalAddresses=plan.Local;
                    if(plan.Interface!=null)rule.Interfaces=new object[]{plan.Interface};
                    if(plan.Port!=null)rule.LocalPorts=plan.Port;
                    rule.EdgeTraversal=false;rule.Enabled=true;rules.Add(rule);added.Add(plan.Name);
                }
                finally{Marshal.FinalReleaseComObject(rule);}
            }
        }
        catch
        {
            foreach(string name in added){try{rules.Remove(name);}catch(COMException){}}
            throw;
        }
        finally{Marshal.FinalReleaseComObject(rules);Marshal.FinalReleaseComObject(policy);}
    }
    private static object[] Interfaces()
    {
        var result=new List<object>();
        foreach(var adapter in NetworkInterface.GetAllNetworkInterfaces().Where(x=>x.OperationalStatus==OperationalStatus.Up))
        {
            var properties=adapter.GetIPProperties();
            bool gateway=properties.GatewayAddresses.Any(x=>x.Address.AddressFamily==AddressFamily.InterNetwork&&!x.Address.Equals(IPAddress.Any));
            foreach(var address in properties.UnicastAddresses.Where(x=>x.Address.AddressFamily==AddressFamily.InterNetwork&&x.DuplicateAddressDetectionState==DuplicateAddressDetectionState.Preferred))
            {
                if(result.Count==64)throw new InvalidDataException("helper_invalid");
                result.Add(new {address=address.Address.ToString(),prefix_length=address.PrefixLength,name=adapter.Name,default_route=gateway});
            }
        }
        return result.ToArray();
    }
    private static object[] Processes()
    {
        var result=new List<object>();
        foreach(string name in new[]{"Settlers7R","Settlers7","S7Patcher","dotnet","python","easytier-core"})
        {
            foreach(var process in Process.GetProcessesByName(name))
            {
                using(process)
                {
                    if(result.Count>=4096)throw new InvalidDataException("helper_invalid");
                    string? image=null;
                    try{if(process.HasExited)continue;image=process.MainModule?.FileName;}
                    catch(System.ComponentModel.Win32Exception){}catch(InvalidOperationException){continue;}
                    bool service=name=="S7Patcher";
                    if(image!=null&&name=="dotnet")
                    {
                        var directory=new DirectoryInfo(Path.GetDirectoryName(image)!);
                        service=directory.Name.Equals("dotnet",StringComparison.OrdinalIgnoreCase)&&directory.Parent?.Name.Equals("Runtime",StringComparison.OrdinalIgnoreCase)==true&&
                            File.Exists(Path.Combine(directory.Parent.Parent!.FullName,"Server/S7Patcher.dll"));
                    }
                    result.Add(new {pid=process.Id,name=name+".exe",path=image,s7_service=service});
                }
            }
        }
        return result.ToArray();
    }
    private static object[] Inspect(string prefix)
    {
        if(!Regex.IsMatch(prefix,"\\A(?:S7LAN-[0-9A-F]{16}-|S7ET-[0-9a-f]{32}-)\\z"))throw new InvalidDataException("helper_invalid");
        dynamic policy=Com("HNetCfg.FwPolicy2"),rules=policy.Rules;var result=new List<object>();
        try
        {
            foreach(dynamic rule in (IEnumerable)rules)
            {
                try
                {
                    if(!((string)rule.Name).StartsWith(prefix,StringComparison.Ordinal))continue;
                    result.Add(new {name=(string)rule.Name,program=(string)rule.ApplicationName,protocol=(int)rule.Protocol,
                        profiles=(int)rule.Profiles,remote=(string)rule.RemoteAddresses,local=(string)rule.LocalAddresses,
                        port=(string)rule.LocalPorts,interfaces=(object?)rule.Interfaces,enabled=(bool)rule.Enabled});
                }
                finally{Marshal.FinalReleaseComObject(rule);}
            }
        }
        finally{Marshal.FinalReleaseComObject(rules);Marshal.FinalReleaseComObject(policy);}
        return result.ToArray();
    }
    private static void WriteAtomic(string path,string value)
    {
        string temporary=SafePaths.Plain(path+"."+Guid.NewGuid().ToString("N")+".tmp");
        try{File.WriteAllText(temporary,value,new UTF8Encoding(false));File.Move(temporary,path,true);}
        finally{File.Delete(temporary);}
    }
}
