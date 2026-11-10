using System;
using System.Buffers.Binary;

namespace S7Patcher.Source
{
    // Gold 1.12.1396's native buffer checksum (RVA 0x8456e0), independently
    // checked in bounded x86 emulation with synthetic data. It is an 8-bit
    // transport checksum, not authentication or a gameplay state checksum.
    // Keep this separate from History's 32-bit transport checksum.
    internal static class GoldPrudpChecksum
    {
        internal static byte Compute(ReadOnlySpan<byte> bytes, byte seed)
        {
            uint words = 0;
            int position = 0;
            while (bytes.Length - position >= 4)
            {
                words = unchecked(words + BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(position, 4)));
                position += 4;
            }
            uint folded = seed + (words & 255) + ((words >> 8) & 255)
                + ((words >> 16) & 255) + (words >> 24);
            while (position < bytes.Length) folded += bytes[position++];
            return unchecked((byte)folded);
        }
    }
}
