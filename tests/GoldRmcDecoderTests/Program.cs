using System.Buffers.Binary;
using S7Patcher.Source;

int checks = 0;
void Check(bool value, string message)
{
    checks++;
    if (!value) throw new InvalidOperationException(message);
}

// Expected field positions were independently checked with the original Gold
// stream constructor/readers in bounded CPU emulation. Full dispatch is not run.
foreach (byte protocol in new byte[] { 1, 10, 11, 126 })
foreach (uint call in new uint[] { 0, 1, 0x12345678, uint.MaxValue })
foreach (uint method in new uint[] { 0, 1, 5, 0x7fffffff, uint.MaxValue })
foreach (byte[] arguments in new byte[][] { [], [1, 2, 3], new byte[65523] })
{
    byte[] message = new byte[13 + arguments.Length];
    BinaryPrimitives.WriteUInt32LittleEndian(message, checked((uint)(message.Length - 4)));
    message[4] = (byte)(protocol | 0x80);
    BinaryPrimitives.WriteUInt32LittleEndian(message.AsSpan(5), call);
    BinaryPrimitives.WriteUInt32LittleEndian(message.AsSpan(9), method);
    arguments.CopyTo(message, 13);
    Check(GoldRmcRequestDecoder.TryDecode(message, out var value, out _), "Synthetic Gold request rejected");
    Check(value.ProtocolId == protocol && value.CallId == call && value.MethodId == method
        && value.ArgumentsOffset == 13 && value.ArgumentsLength == arguments.Length
        && message.AsSpan(value.ArgumentsOffset, value.ArgumentsLength).SequenceEqual(arguments),
        "Gold request header or argument slice changed");
    Check(!GoldRmcRequestDecoder.TryDecode(message.AsSpan(0, message.Length - 1), out _, out _), "Truncated request admitted");
    message[4] = protocol;
    Check(!GoldRmcRequestDecoder.TryDecode(message, out _, out var error)
        && error == GoldRmcDecodeError.NotRequest, "Response interpreted as a request");
}

for (int length = 0; length < 13; length++)
    Check(!GoldRmcRequestDecoder.TryDecode(new byte[length], out _, out _), "Short request admitted");
Check(!GoldRmcRequestDecoder.TryDecode(new byte[65537], out _, out var large)
    && large == GoldRmcDecodeError.TooLarge, "Oversized request admitted");
foreach (byte marker in new byte[] { 0x80, 0xff })
{
    byte[] message = new byte[13]; message[0] = 9; message[4] = marker;
    Check(!GoldRmcRequestDecoder.TryDecode(message, out _, out var error)
        && error == GoldRmcDecodeError.UnsupportedProtocolEncoding, "Unmapped protocol encoding admitted");
}
byte[] concatenated = new byte[26]; concatenated[0] = 9; concatenated[4] = 0x8a;
Check(!GoldRmcRequestDecoder.TryDecode(concatenated, out _, out var trailing)
    && trailing == GoldRmcDecodeError.LengthMismatch, "Trailing/coalesced request admitted");
var random = new Random(72126);
for (int sample = 0; sample < 10000; sample++)
{
    byte[] input = new byte[random.Next(512)]; random.NextBytes(input);
    if (GoldRmcRequestDecoder.TryDecode(input, out var value, out _))
        Check(value.ArgumentsOffset == 13 && value.ArgumentsLength >= 0
            && value.ArgumentsOffset + value.ArgumentsLength == input.Length, "Invalid argument slice");
}
Console.WriteLine($"PASS: {checks} Gold RMC framing checks and 10000 malformed samples; no native dispatch, socket or game connection.");
