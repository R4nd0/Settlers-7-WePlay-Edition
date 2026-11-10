using System.Buffers.Binary;
using S7Patcher.Source;

int checks = 0;
void Check(bool value, string message)
{
    checks++;
    if (!value) throw new InvalidOperationException(message);
}

// The expected values in NativeChecksumCases are generated from the pinned
// original Gold function in bounded x86 emulation, not this C# implementation.
foreach (var fixture in NativeChecksumCases.Values)
    Check(GoldPrudpChecksum.Compute(Convert.FromHexString(fixture.Hex), fixture.Seed) == fixture.Checksum,
        "Native Gold checksum fixture mismatch");

byte[] Packet(byte type, byte flags, byte[] payload, byte seed)
{
    int header = type <= 1 ? 14 : type == 2 ? 11 : 10;
    int payloadStart = header + ((flags & 0x40) != 0 ? 2 : 0);
    byte[] data = new byte[payloadStart + payload.Length + 1];
    data[0] = 0x21; data[1] = 0x22; data[2] = (byte)(type | flags); data[3] = 0x7f;
    BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(4), 0x12345678);
    BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(8), 65535);
    if (type <= 1) BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(10), 0x89abcdef);
    if (type == 2) data[10] = 5;
    if ((flags & 0x40) != 0) BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(header), checked((ushort)payload.Length));
    payload.CopyTo(data, payloadStart);
    data[^1] = GoldPrudpChecksum.Compute(data.AsSpan(0, data.Length - 1), seed);
    return data;
}

foreach (byte type in new byte[] { 0, 1, 2, 3, 4 })
foreach (byte flags in new byte[] { 0, 0x08, 0x10, 0x20, 0x40, 0x78 })
foreach (byte seed in new byte[] { 0, 23, 255 })
foreach (byte[] payload in new byte[][] { [], [0], [1, 2, 3, 4, 5] })
{
    byte[] data = Packet(type, flags, payload, seed);
    Check(GoldPrudpPacketDecoder.TryDecode(data, seed, out var value, out var error), "Valid synthetic Gold packet rejected: " + error);
    Check(value.Type == type && value.Flags == flags && value.SourceVirtualPort == 0x21
        && value.DestinationVirtualPort == 0x22 && value.SessionId == 0x7f
        && value.PacketSignature == 0x12345678 && value.SequenceId == 65535,
        "Header fields changed");
    Check(value.ConnectionSignature == (type <= 1 ? 0x89abcdefu : 0)
        && value.Fragment == (type == 2 ? 5 : 0), "Type-specific field changed");
    Check(data.AsSpan(value.PayloadOffset, value.PayloadLength).SequenceEqual(payload), "Payload slice changed");
    byte[] corrupted = (byte[])data.Clone(); corrupted[^1] ^= 1;
    Check(!GoldPrudpPacketDecoder.TryDecode(corrupted, seed, out _, out error)
        && error == GoldPrudpDecodeError.ChecksumMismatch, "Changed checksum admitted");
    Check(!GoldPrudpPacketDecoder.TryDecode(data, unchecked((byte)(seed + 1)), out _, out _), "Foreign checksum seed admitted");
}

for (int length = 0; length < 11; length++)
    Check(!GoldPrudpPacketDecoder.TryDecode(new byte[length], 0, out _, out _), "Short header admitted");
Check(!GoldPrudpPacketDecoder.TryDecode(new byte[65508], 0, out _, out _), "Oversized datagram admitted");
foreach (byte type in new byte[] { 0, 1, 2, 3, 4 })
{
    var data = Packet(type, 0x40, [1, 2, 3], 23);
    Check(!GoldPrudpPacketDecoder.TryDecode(data.AsSpan(0, data.Length - 1), 23, out _, out _), "Truncated declared packet admitted");
    Check(!GoldPrudpPacketDecoder.TryDecode([.. data, 0], 23, out _, out var error)
        && error == GoldPrudpDecodeError.PayloadLengthMismatch, "Trailing/coalesced data admitted");
    data[2] |= 0x80;
    Check(!GoldPrudpPacketDecoder.TryDecode(data, 23, out _, out error)
        && error == GoldPrudpDecodeError.UnsupportedFlags, "Unmapped extension admitted");
}
foreach (byte value in new byte[] { 0x00, 0x0f, 0x90, 0xff })
{
    var data = Packet(0, 0, [], 0); data[0] = value;
    Check(!GoldPrudpPacketDecoder.TryDecode(data, 0, out _, out var error)
        && error == GoldPrudpDecodeError.UnsupportedStream, "Unmapped stream admitted");
}
foreach (byte type in new byte[] { 5, 6, 7 })
{
    var data = Packet(0, 0, [], 0); data[2] = type;
    Check(!GoldPrudpPacketDecoder.TryDecode(data, 0, out _, out var error)
        && error == GoldPrudpDecodeError.UnsupportedType, "Unmapped packet type admitted");
}
var random = new Random(72026);
for (int sample = 0; sample < 10000; sample++)
{
    byte[] input = new byte[random.Next(512)]; random.NextBytes(input);
    if (GoldPrudpPacketDecoder.TryDecode(input, checked((byte)random.Next(256)), out var packet, out _))
        Check(packet.PayloadOffset >= 10 && packet.PayloadLength >= 0
            && packet.PayloadOffset + packet.PayloadLength == input.Length - 1,
            "Accepted malformed sample has an invalid payload range");
}
Console.WriteLine($"PASS: {checks} Gold checksum/header checks and 10000 malformed samples; no socket, game or multiplayer test.");
