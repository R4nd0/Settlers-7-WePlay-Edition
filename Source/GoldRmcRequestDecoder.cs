using System;
using System.Buffers.Binary;

namespace S7Patcher.Source
{
    // Passive Gold RMC request framing only. Numeric protocol/method IDs are
    // opaque metadata, never authorization to answer or authenticate a caller.
    // The History named-RPC service does not call this decoder.
    internal static class GoldRmcRequestDecoder
    {
        internal const int MaximumMessageLength = 65536; // Local bound, not a native maximum.

        internal static bool TryDecode(ReadOnlySpan<byte> message,
            out GoldRmcRequest metadata, out GoldRmcDecodeError error)
        {
            metadata = default;
            error = GoldRmcDecodeError.None;
            if (message.Length > MaximumMessageLength)
                return Fail(GoldRmcDecodeError.TooLarge, out error);
            if (message.Length < 13)
                return Fail(GoldRmcDecodeError.Truncated, out error);
            if (BinaryPrimitives.ReadUInt32LittleEndian(message) != (uint)(message.Length - 4))
                return Fail(GoldRmcDecodeError.LengthMismatch, out error);
            byte protocol = (byte)(message[4] & 0x7f);
            if ((message[4] & 0x80) == 0)
                return Fail(GoldRmcDecodeError.NotRequest, out error);
            if (protocol is 0 or 0x7f)
                return Fail(GoldRmcDecodeError.UnsupportedProtocolEncoding, out error);
            metadata = new(protocol,
                BinaryPrimitives.ReadUInt32LittleEndian(message.Slice(5, 4)),
                BinaryPrimitives.ReadUInt32LittleEndian(message.Slice(9, 4)),
                13, message.Length - 13);
            return true;
        }

        private static bool Fail(GoldRmcDecodeError reason, out GoldRmcDecodeError error)
        {
            error = reason;
            return false;
        }
    }

    internal readonly record struct GoldRmcRequest(byte ProtocolId, uint CallId,
        uint MethodId, int ArgumentsOffset, int ArgumentsLength);

    internal enum GoldRmcDecodeError
    {
        None, TooLarge, Truncated, LengthMismatch, NotRequest, UnsupportedProtocolEncoding
    }
}
