using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Text.Json;

namespace S7Patcher.Source
{
    // Admission to an explicitly selected LAN coordinator, never game-session
    // authentication. HTTP workers own leases under this lock; only the UDP
    // owner may dispose native protocol contexts after a generation change.
    internal sealed class LocalLobbyAdmission
    {
        internal const int MaximumMembers = 8, LeaseSeconds = 15, BodyLimit = 256;
        internal const string Format = "s7-lobby-control-v1", Prefix = "/public/lobby/v1/";
        private readonly object gate = new();
        private readonly IPAddress coordinator;
        private readonly Func<long> clock;
        private readonly Dictionary<IPAddress, Lease> leases = new();
        private readonly Dictionary<IPAddress, Rate> rates = new();
        private uint revision = 1;
        private ulong generation = 1;
        private sealed record Lease(long Until, ulong Generation);
        private sealed record Rate(long Start, int Count);
        internal string ContentId { get; }
        internal static bool ValidContentId(string value) => value?.Length == 64
            && value.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f');
        internal LocalLobbyAdmission(IPAddress coordinator, string contentId, Func<long> clock = null)
        { this.coordinator = coordinator; ContentId = contentId; this.clock = clock ?? (() => Environment.TickCount64); }

        private void Expire(long now)
        {
            foreach (var address in leases.Where(x => now >= x.Value.Until).Select(x => x.Key).ToArray())
            { leases.Remove(address); AdvanceRevision(); }
        }
        private void AdvanceRevision() { revision = revision == uint.MaxValue ? 1 : revision + 1; }
        internal ulong Generation(IPAddress address)
        {
            lock (gate)
            {
                Expire(clock());
                return leases.TryGetValue(address, out var lease) ? lease.Generation : 0;
            }
        }
        // Linearize a synchronous protocol operation with admission changes.
        // Callers must never await inside action or mutate protocol dictionaries
        // on an HTTP worker; the UDP owner remains their sole writer.
        internal bool TryUseGeneration(IPAddress address, ulong expected, Action action)
        {
            lock (gate)
            {
                Expire(clock());
                if (expected == 0 || !leases.TryGetValue(address, out var lease) || lease.Generation != expected) return false;
                action(); return true;
            }
        }
        internal int Members { get { lock (gate) { Expire(clock()); return leases.Keys.Count(x => !coordinator.Equals(x)) + 1; } } }
        internal bool IsCoordinator(IPAddress address) => coordinator.Equals(address);

        internal (int Status, byte[] Body) Handle(IPAddress source, string method, string path,
            ReadOnlySpan<byte> body, int sessionCount, Func<IPAddress, byte[]> publicGames = null)
        {
            lock (gate)
            {
                long now = clock(); Expire(now);
                if (source == null || !LocalNetworkPolicy.TryPrivateAddress(source.ToString(), out _)) return (403, []);
                bool member = coordinator.Equals(source) || leases.ContainsKey(source);
                // Bound per-source state independently of admission slots. Wrong
                // content or request floods can never remove an existing member.
                foreach (var key in rates.Where(x => now - x.Value.Start >= 1000).Select(x => x.Key).ToArray()) rates.Remove(key);
                if (!rates.TryGetValue(source, out var rate))
                {
                    if (rates.Count >= 128)
                    {
                        // Unadmitted discovery floods must not consume the
                        // bookkeeping slot needed to renew a live member.
                        if (!member) return (429, []);
                        var retired = rates.Keys.FirstOrDefault(x => !coordinator.Equals(x) && !leases.ContainsKey(x));
                        if (retired == null) return (429, []);
                        rates.Remove(retired);
                    }
                    rate = new(now, 0);
                }
                rates[source] = rate with { Count = rate.Count + 1 };
                if (rate.Count >= 12) return (429, []);
                if (path == Prefix + "games")
                {
                    if (method != "GET") return (405, []);
                    if (!member) return (403, []);
                    if (publicGames == null) return (503, []);
                    using MemoryStream output = new();
                    using (Utf8JsonWriter writer = new(output))
                    {
                        writer.WriteStartObject();writer.WriteString("format", "s7-lobby-games-v1");
                        writer.WriteString("content_id", ContentId);writer.WritePropertyName("games");
                        writer.WriteRawValue(publicGames(source));writer.WriteEndObject();
                    }
                    return (200, output.ToArray());
                }
                if (path == Prefix + "status")
                    return method != "GET" ? (405, []) : (200, Encode(false, sessionCount));
                if (path == Prefix + "peers")
                    return method != "GET" ? (405, []) : !member ? (403, []) : (200, Encode(true, sessionCount));
                if (path != Prefix + "admit" && path != Prefix + "leave") return (404, []);
                if (method != "POST") return (405, []);
                if (body.Length is 0 or > BodyLimit || !ReadBody(body, out string content)) return (400, []);
                if (content != ContentId) return (409, []);
                if (path.EndsWith("leave", StringComparison.Ordinal))
                {
                    if (leases.Remove(source)) AdvanceRevision();
                    return (200, []);
                }
                if (!leases.TryGetValue(source, out var lease))
                {
                    if (!coordinator.Equals(source) && leases.Keys.Count(x => !coordinator.Equals(x)) >= MaximumMembers - 1) return (409, []);
                    if (generation == ulong.MaxValue) return (503, []);
                    lease = new(now, ++generation); AdvanceRevision();
                }
                leases[source] = lease with { Until = now + LeaseSeconds * 1000 };
                return (200, Encode(true, sessionCount));
            }
        }
        private static bool ReadBody(ReadOnlySpan<byte> bytes, out string content)
        {
            content = null;
            try
            {
                using var document = JsonDocument.Parse(bytes.ToArray(), new JsonDocumentOptions { MaxDepth = 2 });
                if (document.RootElement.ValueKind != JsonValueKind.Object) return false;
                int count = 0; bool format = false;
                foreach (var field in document.RootElement.EnumerateObject())
                {
                    ++count;
                    if (field.Value.ValueKind != JsonValueKind.String) return false;
                    if (field.NameEquals("format") && !format) format = field.Value.GetString() == Format;
                    else if (field.NameEquals("content_id") && content == null) content = field.Value.GetString();
                    else return false;
                }
                return count == 2 && format && ValidContentId(content);
            }
            catch (JsonException) { return false; }
        }
        private byte[] Encode(bool peers, int sessions)
        {
            using MemoryStream stream = new();
            using (Utf8JsonWriter writer = new(stream))
            {
                writer.WriteStartObject(); writer.WriteString("format", Format); writer.WriteString("content_id", ContentId);
                if (peers)
                {
                    writer.WriteNumber("revision", revision); writer.WriteNumber("lease_seconds", LeaseSeconds);
                    writer.WriteStartArray("peers"); writer.WriteStringValue(coordinator.ToString());
                    foreach (var address in leases.Keys.Where(x => !coordinator.Equals(x)).OrderBy(x => x.ToString(), StringComparer.Ordinal)) writer.WriteStringValue(address.ToString());
                    writer.WriteEndArray();
                }
                else
                {
                    writer.WriteNumber("members", leases.Keys.Count(x => !coordinator.Equals(x)) + 1);
                    writer.WriteNumber("active_members", leases.Count); writer.WriteNumber("max_members", MaximumMembers);
                    writer.WriteNumber("sessions", Math.Clamp(sessions, 0, MaximumMembers));
                }
                writer.WriteEndObject();
            }
            return stream.ToArray();
        }
    }
}
