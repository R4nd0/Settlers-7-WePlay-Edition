using System.Diagnostics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using WePlay.Helpers;

internal static class Program
{
    private const string Repository="R4nd0/Settlers-7-WePlay-Edition";
    private static string? app,updates,version;
    private static int sequence;

    private static int Main(string[] args)
    {
        bool validate=args.Length==2&&args[0]=="--validate";
        Mutex? mutex=null;bool owned=false,statusAllowed=false;
        try
        {
            if(args.Length!=2||args[0] is not ("--job" or "--validate"))Fail("update_invalid");
            string jobFile=SafePaths.Plain(args[1]);string job=Path.GetDirectoryName(jobFile)!;
            updates=Path.GetDirectoryName(job)!;string state=Path.GetDirectoryName(updates)!;app=Path.GetDirectoryName(state)!;
            if(Path.GetFileName(jobFile)!="handoff.json"||!Match(Path.GetFileName(job),"[0-9a-f]{32}")||
               !Equal(Path.GetFileName(updates),"Updates")||!Equal(Path.GetFileName(state),"State"))Fail("update_invalid");
            using var record=Read(jobFile,4096);
            var r=record.RootElement;Keys(r,"format","app","parent","backend","version","sequence");
            if(Text(r,"format")!="weplay-update-handoff-v1"||!Equal(SafePaths.Plain(Text(r,"app")),app))Fail("update_invalid");
            version=Text(r,"version");sequence=r.GetProperty("sequence").GetInt32();
            string self=SafePaths.FileAt(job,"WePlay.UpdateHelper.exe");
            string trusted=SafePaths.FileAt(app,"Package/Payload/Helpers/WePlay.UpdateHelper.exe");
            if(!Equal(SafePaths.Plain(Environment.ProcessPath!),self)||SafePaths.Hash(self)!=SafePaths.Hash(trusted))Fail("update_invalid");
            statusAllowed=true;

            using var keyStream=Assembly.GetExecutingAssembly().GetManifestResourceStream("WePlay.UpdateKey")!;
            using var key=JsonDocument.Parse(keyStream);
            var pin=key.RootElement;Keys(pin,"format","key_id","n","e");
            if(Text(pin,"format")!="weplay-update-key-v1"||pin.GetProperty("e").GetInt32()!=65537||!Match(Text(pin,"n"),"[0-9a-f]{768}"))Fail("update_signature_invalid");
            byte[] modulus=Convert.FromHexString(Text(pin,"n"));
            if(modulus[0]<128||(modulus[^1]&1)==0||Text(pin,"key_id")!=Convert.ToHexStringLower(SHA256.HashData(modulus)))Fail("update_signature_invalid");
            using var envelope=Read(SafePaths.FileAt(job,"weplay-update.json"),49152);
            var e=envelope.RootElement;Keys(e,"format","key_id","payload","signature");
            if(Text(e,"format")!="weplay-signed-update-v1"||Text(e,"key_id")!=Text(pin,"key_id"))Fail("update_signature_invalid");
            byte[] payload=Decode64(Text(e,"payload")),signature=Decode64(Text(e,"signature"));
            if(payload.Length>32768||signature.Length!=384)Fail("update_signature_invalid");
            using(var rsa=RSA.Create())
            {
                rsa.ImportParameters(new RSAParameters{Modulus=modulus,Exponent=[1,0,1]});
                if(!rsa.VerifyData(payload,signature,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1))Fail("update_signature_invalid");
            }
            using var manifest=JsonDocument.Parse(payload);var m=manifest.RootElement;
            Keys(m,"format","repository","channel","sequence","version","tag","notes","setup");
            string tag=Text(m,"tag"),notes=Text(m,"notes");
            if(Text(m,"format")!="weplay-update-v1"||Text(m,"repository")!=Repository||Text(m,"channel")!="preview"||
               m.GetProperty("sequence").GetInt32()!=sequence||sequence<1||Text(m,"version")!=version||
               !Match(version!,"[0-9]{1,4}\\.[0-9]{1,4}\\.[0-9]{1,4}(?:-[A-Za-z0-9][A-Za-z0-9.-]{0,47})?")||
               !Match(tag,"[A-Za-z0-9][A-Za-z0-9._-]{0,79}")||notes.Length>4000||notes.Any(c=>c<32&&c is not ('\n' or '\r' or '\t')))Fail("update_invalid");
            var setup=m.GetProperty("setup");Keys(setup,"name","url","size","sha256");
            long size=setup.GetProperty("size").GetInt64();string digest=Text(setup,"sha256");
            if(Text(setup,"name")!="Setup.exe"||Text(setup,"url")!=$"https://github.com/{Repository}/releases/download/{tag}/Setup.exe"||
               size<1||size>805306368||!Match(digest,"[0-9a-f]{64}"))Fail("update_invalid");
            using var local=Read(SafePaths.FileAt(app,"desktop-release.json"),65536);
            using var highest=Read(SafePaths.FileAt(updates,"highest.json"),4096);
            Keys(highest.RootElement,"sequence","payload_sha256");
            if(sequence<=local.RootElement.GetProperty("update_sequence").GetInt32()||Text(local.RootElement,"update_channel")!="preview"||
               sequence!=highest.RootElement.GetProperty("sequence").GetInt32()||
               Text(highest.RootElement,"payload_sha256")!=Convert.ToHexStringLower(SHA256.HashData(payload)))Fail("update_downgrade");
            string installer=SafePaths.FileAt(job,"Setup.exe");VerifySetup(installer,size,digest);
            if(validate){Console.WriteLine("PASS: compiled helper verified signed update and installer; no installation performed.");return 0;}

            string appKey=Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(app.ToLowerInvariant())))[..24];
            mutex=new Mutex(true,"Local\\S7Launcher-Update-"+appKey,out bool created);
            if(!created)Fail("update_busy");owned=true;
            using var parent=OpenOwned(r.GetProperty("parent"),SafePaths.FileAt(app,"S7Launcher.exe"));
            using var backend=OpenOwned(r.GetProperty("backend"),SafePaths.FileAt(app,"Package/Runtime/python/python.exe"));
            Status("waiting","");
            Atomic(Path.Combine(job,"ready.json"),writer=>
            {
                writer.WriteString("format","weplay-update-ready-v1");writer.WriteNumber("helper_pid",Environment.ProcessId);
                writer.WriteString("version",version);writer.WriteNumber("sequence",sequence);
            },overwrite:false);
            if(!parent.WaitForExit(180000)||!backend.WaitForExit(180000))Fail("update_busy");
            EnsureClosed();Status("installing","");
            // Keep the verified file non-writable/non-replaceable through the final execution boundary.
            using(var locked=new FileStream(SafePaths.Plain(installer),FileMode.Open,FileAccess.Read,FileShare.Read))
            {
                if(locked.Length!=size||Convert.ToHexStringLower(SHA256.HashData(locked))!=digest)Fail("update_hash_invalid");
                var start=new ProcessStartInfo(installer){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=job};
                foreach(string argument in new[]{"/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART","/DIR="+app})start.ArgumentList.Add(argument);
                using var process=Process.Start(start)??throw new InvalidDataException("update_install_failed");
                process.WaitForExit();if(process.ExitCode!=0)Fail("update_install_failed");
            }
            Status("installed","");mutex.ReleaseMutex();owned=false;mutex.Dispose();mutex=null;
            Process.Start(new ProcessStartInfo(SafePaths.FileAt(app,"S7Launcher.exe")){UseShellExecute=false,WorkingDirectory=app});
            return 0;
        }
        catch(Exception error)
        {
            string code=Match(error.Message,"update_[a-z_]+")?error.Message:error.Message.StartsWith("helper_",StringComparison.Ordinal)?"update_invalid":"update_handoff_failed";
            if(!validate&&statusAllowed&&app!=null&&updates!=null){try{Status("failed",code);}catch(Exception){}}
            if(validate)Console.WriteLine("FAIL: "+code);
            return 2;
        }
        finally{if(mutex!=null){if(owned)mutex.ReleaseMutex();mutex.Dispose();}}
    }
    private static void EnsureClosed()
    {
        foreach(string name in new[]{"Settlers7R","Settlers7","S7Patcher"})
        {
            var rows=Process.GetProcessesByName(name);
            try{if(rows.Length>0)Fail("update_busy");}finally{foreach(var row in rows)row.Dispose();}
        }
        foreach(var process in Process.GetProcessesByName("dotnet"))
        {
            using(process)
            {
                try
                {
                    if(process.HasExited)continue;
                    string image=process.MainModule?.FileName??throw new InvalidDataException("update_busy");
                    var directory=new DirectoryInfo(Path.GetDirectoryName(image)!);
                    // S7 owns a bundled host under <copy>/Runtime/dotnet, beside Server.
                    if(directory.Name.Equals("dotnet",StringComparison.OrdinalIgnoreCase)&&directory.Parent?.Name.Equals("Runtime",StringComparison.OrdinalIgnoreCase)==true&&
                       File.Exists(Path.Combine(directory.Parent.Parent!.FullName,"Server/S7Patcher.dll")))Fail("update_busy");
                }
                catch(InvalidOperationException){}catch(System.ComponentModel.Win32Exception){Fail("update_busy");}
            }
        }
    }
    private static Process OpenOwned(JsonElement value,string expected)
    {
        Keys(value,"pid","path","started_ticks");
        if(!Equal(SafePaths.Plain(Text(value,"path")),expected))Fail("update_parent_invalid");
        Process process;
        try{process=Process.GetProcessById(value.GetProperty("pid").GetInt32());}
        catch(ArgumentException){throw new InvalidDataException("update_parent_invalid");}
        try
        {
            _=process.Handle;
            if(!Equal(process.MainModule?.FileName??"",expected)||process.StartTime.ToUniversalTime().Ticks!=value.GetProperty("started_ticks").GetInt64())Fail("update_parent_invalid");
            return process;
        }
        catch{process.Dispose();throw;}
    }
    private static void VerifySetup(string path,long size,string hash)
    {if(new FileInfo(path).Length!=size||SafePaths.Hash(path)!=hash)Fail("update_hash_invalid");}
    private static void Status(string phase,string code)=>Atomic(SafePaths.Plain(Path.Combine(updates!,"status.json")),writer=>
    {
        writer.WriteString("format","weplay-update-status-v1");writer.WriteString("phase",phase);writer.WriteString("code",code);
        writer.WriteString("version",version);writer.WriteNumber("sequence",sequence);
    });
    private static void Atomic(string path,Action<Utf8JsonWriter> content,bool overwrite=true)
    {
        string temporary=SafePaths.Plain(path+"."+Guid.NewGuid().ToString("N")+".tmp");
        try
        {
            using(var stream=new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.None))
            {using(var writer=new Utf8JsonWriter(stream)){writer.WriteStartObject();content(writer);writer.WriteEndObject();writer.Flush();}stream.Flush(true);}
            File.Move(temporary,path,overwrite);
        }
        finally{File.Delete(temporary);}
    }
    private static JsonDocument Read(string path,int limit)
    {
        path=SafePaths.Plain(path);if(new FileInfo(path).Length>limit)Fail("update_invalid");
        var value=JsonDocument.Parse(File.ReadAllBytes(path));Unique(value.RootElement);return value;
    }
    private static void Unique(JsonElement value)
    {
        if(value.ValueKind==JsonValueKind.Object)
        {var seen=new HashSet<string>(StringComparer.Ordinal);foreach(var item in value.EnumerateObject()){if(!seen.Add(item.Name))Fail("update_invalid");Unique(item.Value);}}
        else if(value.ValueKind==JsonValueKind.Array)foreach(var item in value.EnumerateArray())Unique(item);
    }
    private static void Keys(JsonElement value,params string[] keys)
    {
        if(value.ValueKind!=JsonValueKind.Object)Fail("update_invalid");
        Unique(value);var names=value.EnumerateObject().Select(x=>x.Name).ToHashSet(StringComparer.Ordinal);
        if(!names.SetEquals(keys))Fail("update_invalid");
    }
    private static byte[] Decode64(string value)
    {
        if(!Match(value,"[A-Za-z0-9_-]+"))Fail("update_signature_invalid");
        byte[] bytes=Convert.FromBase64String(value.Replace('-','+').Replace('_','/')+new string('=',(4-value.Length%4)%4));
        if(Convert.ToBase64String(bytes).TrimEnd('=').Replace('+','-').Replace('/','_')!=value)Fail("update_signature_invalid");
        return bytes;
    }
    private static string Text(JsonElement value,string key)=>value.GetProperty(key).GetString()??throw new InvalidDataException("update_invalid");
    private static bool Equal(string a,string b)=>string.Equals(a,b,StringComparison.OrdinalIgnoreCase);
    private static bool Match(string text,string pattern)=>Regex.IsMatch(text,"\\A(?:"+pattern+")\\z",RegexOptions.CultureInvariant,TimeSpan.FromSeconds(1));
    private static void Fail(string code)=>throw new InvalidDataException(code);
}
