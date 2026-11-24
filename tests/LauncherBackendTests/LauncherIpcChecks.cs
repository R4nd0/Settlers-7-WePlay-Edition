using System.Diagnostics;
using System.IO.Pipes;
using System.Reflection;
using System.Text;
using S7Desktop;

internal static partial class Tests
{
    private static async Task LauncherIpc(string root)
    {
        var failures=new List<string>();
        async Task Verify(string name,Func<Task> exercise)
        {
            try{await exercise();}
            catch(Exception error){failures.Add(name+" — "+error.Message);}
        }

        await Verify("already-cancelled-launcher-query-does-not-start",async()=>{
            using var stop=new CancellationTokenSource();stop.Cancel();
            var command=new ProcessStartInfo(Path.Combine(root,"never-start-this-query")) {
                RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true
            };
            await Expect("cancelled",()=>Backend.RunProcess(command,root,Guid.NewGuid().ToString("N"),
                "directory-list",new {},_=>{},false,stop.Token,FaultTiming));
        });

        await Verify("explicit-launcher-cancel-reports-cancelled",async()=>{
            string folder=Path.Combine(root,"explicit-cancel");Directory.CreateDirectory(folder);
            string id=Guid.NewGuid().ToString("N");
            var start=new ProcessStartInfo(Environment.ProcessPath!) {
                UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,
                RedirectStandardOutput=true,RedirectStandardError=true,StandardInputEncoding=new UTF8Encoding(false)
            };
            if(Path.GetFileNameWithoutExtension(Environment.ProcessPath!).Equals("dotnet",StringComparison.OrdinalIgnoreCase))
                start.ArgumentList.Add(Assembly.GetExecutingAssembly().Location);
            foreach(string argument in new[]{"--child","hang",folder,id})start.ArgumentList.Add(argument);
            var timing=FaultTiming with{Idle=TimeSpan.FromSeconds(10)};
            Task run=Backend.RunProcess(start,folder,id,"diagnostics",new {},_=>{},true,default,timing);
            var wait=Stopwatch.StartNew();
            while(!File.Exists(Path.Combine(folder,"ready"))&&!run.IsCompleted&&wait.Elapsed<TimeSpan.FromSeconds(5))
                await Task.Delay(20);
            Check(File.Exists(Path.Combine(folder,"ready")),"explicit cancel fixture became ready");
            Backend.Cancel();
            await Expect("cancelled",()=>run.WaitAsync(TimeSpan.FromSeconds(15)));
        });

        await Verify("duplicate-terminal-result-is-refused",()=>
            Scenario(root,"duplicate-result","directory-list","backend_protocol_invalid",null));
        await Verify("non-object-terminal-result-is-refused",()=>
            Scenario(root,"null-result","directory-list","backend_protocol_invalid",null));

        if(OperatingSystem.IsWindows())
        {
            await Verify("private-pipe-partial-frame-is-a-failure",async()=>{
                var received=new List<string>();
                await Expect("internet_channel_failed",()=>ExchangePrivateFrames(
                    [..BitConverter.GetBytes(8),(byte)'{'],received));
                Check(received.Count==0,"an incomplete private frame never reaches the launcher");
                await ExchangePrivateFrames([],received);
                Check(received.Count==0,"normal EOF between frames is still accepted");
            });
            await Verify("unexpected-private-code-is-refused",async()=>{
                var received=new List<string>();
                var bytes=Encoding.UTF8.GetBytes("""{"format":"weplay-internet-private-v1","invitation":"unsupported response"}""");
                await Expect("internet_channel_failed",()=>ExchangePrivateFrames(
                    [..BitConverter.GetBytes(bytes.Length),..bytes],received));
                Check(received.Count==0,"an unrecognized invitation never reaches the launcher");
            });
        }

        if(failures.Count>0)throw new Exception(string.Join(Environment.NewLine,failures));
    }

    private static async Task ExchangePrivateFrames(byte[] reply,List<string> received)
    {
        using var channel=new InternetChannel();
        Task server=channel.Run(null,received.Add);
        using var stop=new CancellationTokenSource(TimeSpan.FromSeconds(5));
        using(var client=new NamedPipeClientStream(".",channel.Name,PipeDirection.InOut,PipeOptions.Asynchronous))
        {
            await client.ConnectAsync(stop.Token);
            byte[] header=new byte[4];
            await client.ReadExactlyAsync(header,stop.Token);
            byte[] request=new byte[BitConverter.ToInt32(header)];
            await client.ReadExactlyAsync(request,stop.Token);
            if(reply.Length>0)await client.WriteAsync(reply,stop.Token);
        }
        await server.WaitAsync(stop.Token);
    }
}
