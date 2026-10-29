using System.IO.Pipes;
using System.Text.Json;

namespace S7Desktop;

/// <summary>Invitation secrets never enter the normal backend event stream.</summary>
internal sealed class InternetChannel : IDisposable
{
    private const string Format = "weplay-internet-private-v1";
    private readonly NamedPipeServerStream pipe;
    private readonly CancellationTokenSource stop = new();
    private bool disposed;
    public string Name { get; } = "weplay-internet-" + Guid.NewGuid().ToString("N");
    public InternetChannel()
    {
        pipe = new NamedPipeServerStream(Name, PipeDirection.InOut, 1, PipeTransmissionMode.Byte,
            PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly, 8192, 8192);
    }
    private async Task WriteFrame(object value)
    {
        byte[] raw = JsonSerializer.SerializeToUtf8Bytes(value);
        if (raw.Length is < 1 or > 4096) throw new BackendError("internet_channel_failed", "");
        await pipe.WriteAsync(BitConverter.GetBytes(raw.Length), stop.Token).ConfigureAwait(false);
        await pipe.WriteAsync(raw, stop.Token).ConfigureAwait(false);
        await pipe.FlushAsync(stop.Token).ConfigureAwait(false);
    }
    private async Task<JsonElement> ReadFrame()
    {
        byte[] header = new byte[4]; await pipe.ReadExactlyAsync(header, stop.Token).ConfigureAwait(false);
        int length = BitConverter.ToInt32(header);
        if (length is < 1 or > 4096) throw new BackendError("internet_channel_failed", "");
        byte[] raw = new byte[length]; await pipe.ReadExactlyAsync(raw, stop.Token).ConfigureAwait(false);
        using var document = JsonDocument.Parse(raw); return document.RootElement.Clone();
    }
    public async Task Run(string? invitation, Action<string> receive)
    {
        try
        {
            await pipe.WaitForConnectionAsync(stop.Token).ConfigureAwait(false);
            await WriteFrame(new { format = Format, invitation }).ConfigureAwait(false);
            while (!stop.IsCancellationRequested)
            {
                JsonElement value = await ReadFrame().ConfigureAwait(false);
                if (value.GetProperty("format").GetString() != Format || value.EnumerateObject().Count() != 2)
                    throw new BackendError("internet_channel_failed", "");
                string? code = value.GetProperty("invitation").GetString();
                if (code == null || code.Length is < 1 or > 2048) throw new BackendError("internet_channel_failed", "");
                receive(code);
            }
        }
        catch (OperationCanceledException) when (stop.IsCancellationRequested) { }
        catch (ObjectDisposedException) when (stop.IsCancellationRequested) { }
        catch (EndOfStreamException) { /* Backend closes the private pipe before its public result. */ }
        catch (Exception error) when (error is IOException or JsonException or InvalidOperationException or KeyNotFoundException)
        { throw new BackendError("internet_channel_failed", ""); }
    }
    public void Dispose() { if(disposed)return;disposed=true;stop.Cancel();pipe.Dispose(); }
}
