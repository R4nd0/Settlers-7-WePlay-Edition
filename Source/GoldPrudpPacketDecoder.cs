using System;
using System.Buffers.Binary;

namespace S7Patcher.Source
{
    // Passive, single-datagram Gold development codec. No socket, reply,
    // decryption, ticket, identity or admission. The History service does not
    // call this class and must never auto-downgrade to it on a decode failure.
    internal static class GoldPrudpPacketDecoder
    {
        internal const int MaximumDatagramLength = 65507;

        internal static bool TryDecode(ReadOnlySpan<byte> bytes, byte checksumSeed,
            out GoldPrudpPacket metadata, out GoldPrudpDecodeError error)
        {
            metadata = default;
            error = GoldPrudpDecodeError.None;
            if (bytes.Length > MaximumDatagramLength) return Fail(GoldPrudpDecodeError.TooLarge, out error);
            if (bytes.Length < 11) return Fail(GoldPrudpDecodeError.Truncated, out error);
            int sourceStream = bytes[0] >> 4, destinationStream = bytes[1] >> 4;
            if (sourceStream is < 1 or > 8 || destinationStream is < 1 or > 8)
                return Fail(GoldPrudpDecodeError.UnsupportedStream, out error);
            byte type = (byte)(bytes[2] & 7), flags = (byte)(bytes[2] & 0xf8);
            if (type > 4) return Fail(GoldPrudpDecodeError.UnsupportedType, out error);
            // Flooded/extended traffic and coalesced packets require separate
            // native mapping. Do not interpret History's crypto extension here.
            if ((flags & 0x80) != 0) return Fail(GoldPrudpDecodeError.UnsupportedFlags, out error);
            int position = type <= 1 ? 14 : type == 2 ? 11 : 10;
            if (bytes.Length < position + 1) return Fail(GoldPrudpDecodeError.Truncated, out error);
            uint connection = type <= 1 ? BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(10, 4)) : 0;
            byte fragment = type == 2 ? bytes[10] : (byte)0;
            if ((flags & 0x40) != 0)
            {
                if (bytes.Length < position + 3) return Fail(GoldPrudpDecodeError.Truncated, out error);
                int declared = BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(position, 2));
                position += 2;
                if (declared != bytes.Length - position - 1)
                    return Fail(GoldPrudpDecodeError.PayloadLengthMismatch, out error);
            }
            if (GoldPrudpChecksum.Compute(bytes[..^1], checksumSeed) != bytes[^1])
                return Fail(GoldPrudpDecodeError.ChecksumMismatch, out error);
            metadata = new(type, flags, bytes[0], bytes[1], bytes[3],
                BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(4, 4)),
                BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(8, 2)),
                connection, fragment, position, bytes.Length - position - 1);
            return true;
        }

        private static bool Fail(GoldPrudpDecodeError reason, out GoldPrudpDecodeError error)
        {
            error = reason;
            return false;
        }
    }

    internal readonly record struct GoldPrudpPacket(byte Type, byte Flags,
        byte SourceVirtualPort, byte DestinationVirtualPort, byte SessionId,
        uint PacketSignature, ushort SequenceId, uint ConnectionSignature,
        byte Fragment, int PayloadOffset, int PayloadLength);

    internal enum GoldPrudpDecodeError
    {
        None, TooLarge, Truncated, UnsupportedStream, UnsupportedType,
        UnsupportedFlags, PayloadLengthMismatch, ChecksumMismatch
    }
}
