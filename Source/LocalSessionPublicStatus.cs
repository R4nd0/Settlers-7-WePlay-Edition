using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace S7Patcher.Source
{
    internal sealed partial class LocalSessionRegistry
    {
        // UDP builds immutable rows; HTTP never touches live dictionaries.
        // Owner addresses only filter access and are never serialized.
        private sealed record PublicGame(IPAddress Owner, uint Id, string Title, string State,
            int Players, int Capacity, int Free);
        private PublicGame[] publicGames = [];

        internal byte[] PublicGamesFor(IPAddress viewer)
        {
            using MemoryStream stream = new();
            using (Utf8JsonWriter writer = new(stream))
            {
                writer.WriteStartArray();
                foreach (var game in Volatile.Read(ref publicGames))
                {
                    if (!game.Owner.Equals(viewer)) continue;
                    writer.WriteStartObject();
                    writer.WriteString("id", game.Id.ToString("x8", CultureInfo.InvariantCulture));
                    writer.WriteString("title", game.Title); writer.WriteString("state", game.State);
                    writer.WriteNumber("players", game.Players); writer.WriteNumber("capacity", game.Capacity);
                    writer.WriteNumber("free_slots", game.Free); writer.WriteEndObject();
                }
                writer.WriteEndArray();
            }
            return stream.ToArray();
        }

        private void RefreshPublicGames()
        {
            var result = new List<PublicGame>();
            if (!closed)
                foreach (var entry in entries)
                {
                    var attributes = entry.Metadata.Attributes;
                    if (!attributes.TryGetValue(JoinKey("private"), out var privacy)
                        || !privacy.AsSpan().SequenceEqual(new byte[] { 3, 0 })
                        || !LocalSessionDisplayCodec.HasNativeAttributes(entry.Metadata)
                        || !LocalLoginRpcEngine.PrudpUrl.TryRead(entry.Host.PublicUrl, out var registered)
                        || !Slots(entry.Metadata, "settlers_max_slots", out long maximum)
                        || !Slots(entry.Metadata, "settlers_free_slots", out long available)) continue;
                    int confirmed = 0, pending = 0;
                    foreach (var member in entry.Pending.Values)
                        if (member.Confirmed) ++confirmed; else ++pending;
                    int players = 1 + confirmed;
                    string state;
                    int free;
                    if (maximum >= 1)
                    {
                        entry.LastPublicCapacity = (int)maximum;
                        free = (int)Math.Max(0, Math.Min(available - pending, maximum - players - pending));
                        state = free > 0 ? "open" : "full";
                    }
                    else if (available == 0 && entry.LastPublicCapacity > 0)
                    {
                        // The fingerprinted hostLobby start path calls
                        // SetSlotCounts(0,0) immediately before StartGame().
                        // This is a start/loading signal, not simulation progress.
                        free = 0; state = "started";
                    }
                    else continue;
                    if (players > entry.LastPublicCapacity) continue;
                    result.Add(new(registered.Address, entry.Id, PublicTitle(attributes), state,
                                   players, entry.LastPublicCapacity, free));
                }
            Volatile.Write(ref publicGames, result.ToArray());
        }

        private static string PublicTitle(Dictionary<string, byte[]> attributes)
        {
            const string fallback = "Siedler 7";
            if (!attributes.TryGetValue(JoinKey("settlers_session_name"), out var value)
                || value.Length is < 4 or > 132 || value[0] != 4 || value[^1] != 0
                || BinaryPrimitives.ReadUInt16LittleEndian(value.AsSpan(1)) != value.Length - 3) return fallback;
            string text;
            try { text = new UTF8Encoding(false, true).GetString(value, 3, value.Length - 4); }
            catch (DecoderFallbackException) { return fallback; }
            if (text.Length is < 1 or > 48 || text != text.Trim()) return fallback;
            foreach (char character in text)
                if (char.IsControl(character) || char.GetUnicodeCategory(character) == UnicodeCategory.Format) return fallback;
            foreach (string forbidden in new[] { "@", "://", "weplay1.", "discord.gg" })
                if (text.Contains(forbidden, StringComparison.OrdinalIgnoreCase)) return fallback;
            return text;
        }
    }
}
