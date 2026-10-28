using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using S7Patcher.Source;

internal static class LocalLobbyChecks
{
    private static readonly string Content = new('a', 64);
    private static byte[] Body(string content = null) => Encoding.UTF8.GetBytes(
        "{\"format\":\"s7-lobby-control-v1\",\"content_id\":\"" + (content ?? Content) + "\"}");
    internal static async Task<int> RunAsync()
    {
        int checks = 0;
        void Check(bool value, string label) { if (!value) throw new Exception(label); ++checks; }
        long now = 1000;
        const string coordinator = "192.168.10.10";
        string[] contract = ["--local-config-server", "--local-catalogue-probe", "--lan-lobby", "--lan-address", coordinator,
            "--content-id", Content, "--http-port", "9110", "--udp-port", "9111", "--seconds", "86400"];
        Check(LocalConfigServerCommand.TryParse(contract, out var parsed) && parsed.LanLobby && parsed.Network.Lobby != null
            && parsed.HttpPort == 9110 && parsed.UdpPort == 9111 && parsed.Seconds == 86400, "root launcher exact lobby CLI contract");
        Check(!LocalConfigServerCommand.TryParse([..contract, "--lan-peer", "192.168.10.20"], out _), "lobby pair mode ambiguity rejected");
        Check(!LocalConfigServerCommand.TryParse(["--local-config-server", "--lan-lobby"], out _), "incomplete lobby contract rejected");
        Check(LocalConfigServerCommand.TryParse(["--local-config-server", "--local-catalogue-probe", "--lan-address", coordinator,
            "--lan-peer", "192.168.10.20", "--content-id", Content], out parsed) && !parsed.LanLobby && parsed.Network.Lobby == null, "legacy pair CLI unchanged");
        Check(LocalNetworkPolicy.TryCreateLobby(coordinator, Content, out var network, () => now), "lobby fixture");
        var lobby = network.Lobby;
        IPAddress Address(int n) => IPAddress.Parse("192.168.10." + n);
        (int Status, byte[] Body) Call(int n, string action, string method = "POST", byte[] body = null)
            => lobby.Handle(Address(n), method, LocalLobbyAdmission.Prefix + action, body ?? Body(), 2);
        Check(lobby.Members == 1 && lobby.IsCoordinator(Address(10)) && !network.Allows(Address(10)), "coordinator reserved independently of game lease");
        Check(!network.Allows(Address(20)) && !network.Allows(IPAddress.Loopback), "no private subnet or loopback admission");
        Check(Call(20, "peers", "GET").Status == 403, "unadmitted peer list denied");
        Check(Call(20, "status", "GET").Status == 200, "private discovery status");
        Check(Call(20, "admit", body: Body(new string('b', 64))).Status == 409 && lobby.Members == 1, "content mismatch no admission");
        foreach (int n in Enumerable.Range(20, 7)) Check(Call(n, "admit").Status == 200, "admit distinct source");
        Check(lobby.Members == 8 && Call(40, "admit").Status == 409, "eight addresses includes coordinator");
        using (var peers = JsonDocument.Parse(Call(20, "peers", "GET").Body))
        {
            Check(peers.RootElement.GetProperty("peers").GetArrayLength() == 8, "complete peer list");
            Check(peers.RootElement.GetProperty("peers")[0].GetString() == coordinator, "coordinator always listed");
            Check(peers.RootElement.GetProperty("lease_seconds").GetInt32() == 15, "lease contract");
        }
        ulong first = lobby.Generation(Address(20));
        now += 3000;
        Check(Call(20, "admit").Status == 200 && lobby.Generation(Address(20)) == first, "renew preserves transport generation");
        Check(Call(20, "admit", body: Body(new string('b', 64))).Status == 409
            && lobby.Generation(Address(20)) == first, "bad request cannot evict live peer");
        now = 16000;
        Check(network.Allows(Address(20)) && !network.Allows(Address(21)) && lobby.Members == 2, "independent precise expiry");
        now = 19000;
        Check(!network.Allows(Address(20)) && lobby.Members == 1, "lease lapse closes admission");
        Check(Call(20, "admit").Status == 200 && lobby.Generation(Address(20)) != first, "rejoin gets fresh generation");
        ulong second = lobby.Generation(Address(20));
        Check(Call(20, "leave").Status == 200 && !network.Allows(Address(20)), "leave removes only caller");
        Check(Call(10, "admit").Status == 200 && network.Allows(Address(10)), "coordinator game admits normally");
        Check(Call(10, "leave").Status == 200 && !network.Allows(Address(10)) && lobby.Members == 1, "coordinator game lease can end without losing reservation");
        using (var status = JsonDocument.Parse(Call(10, "status", "GET").Body))
            Check(status.RootElement.GetProperty("active_members").GetInt32() == 0
                && status.RootElement.GetProperty("members").GetInt32() == 1, "idle coordinator reservation is not an active player");
        Check(Call(20, "admit").Status == 200 && lobby.Generation(Address(20)) != second, "leave rejoin fresh context");
        Check(lobby.Handle(IPAddress.Parse("8.8.8.8"), "POST", LocalLobbyAdmission.Prefix + "admit", Body(), 0).Status == 403, "public source denied");
        Check(Call(21, "admit", body: Encoding.UTF8.GetBytes("{\"format\":\"s7-lobby-control-v1\",\"content_id\":\"" + Content + "\",\"extra\":1}")).Status == 400, "strict control body");
        Check(Call(21, "admit", body: new byte[257]).Status == 400, "bounded control body");
        for (int i = 0; i < 12; ++i) Call(22, "status", "GET");
        Check(Call(22, "admit").Status == 429 && lobby.Generation(Address(22)) == 0, "bounded per source rate");
        Check(LocalNetworkPolicy.TryCreate(coordinator, "192.168.10.20", out var pair)
            && pair.Lobby == null && pair.Allows(Address(20)) && !pair.Allows(Address(21)), "legacy pair unchanged");

        // Three distinct registered host IPs share one coordinator. Each route
        // must stay bound to its actual registration, not the coordinator IP.
        now += 1000;
        foreach (int n in new[] { 20, 21, 22 }) Check(Call(n, "admit").Status == 200, "multi host admission");
        using (var registry = new LocalSessionRegistry(catalogueProbe: true))
        {
            foreach (int n in new[] { 20, 21, 22 })
            {
                using var host = RegisteredHostChecks.CreateRegistered(Address(n), network);
                Check(host.TryCopyRegisteredPeer(out var registered), "multi host registration");
                using (registered)
                {
                    string owned = LocalStormRouteChecks.Route(registered.ConnectionId, Address(n) + ":3074", emptyProfile:true, gatewaySentinel:true);
                    Check(registry.TryCreate(new object(), host, LocalSessionCatalogueChecks.PublicBody(route: owned), out _, out _), "separate host session");
                    string foreign = LocalStormRouteChecks.Route(registered.ConnectionId, coordinator + ":3074", emptyProfile:true, gatewaySentinel:true);
                    Check(!LocalSessionRegistry.HostRoutesOwned(Encoding.ASCII.GetBytes(foreign), registered), "coordinator route cannot hijack owner route");
                }
            }
            Check(registry.Count == 3, "three registered hosts coexist");
            Check(LocalSessionCatalogueCodec.TrySearch(LocalSessionDisplayChecks.Search(), registry, new object(), out _, out _), "three host catalogue routes remain valid");
        }
        Check(Call(23, "admit").Status == 200 && Call(24, "admit").Status == 200, "additional distinct guests admitted");
        using (var registry = new LocalSessionRegistry(catalogueProbe: true))
        using (var host = RegisteredHostChecks.CreateRegistered(Address(20), network))
        {
            host.TryCopyRegisteredPeer(out var identity);
            using (identity)
            {
                string route = LocalStormRouteChecks.Route(identity.ConnectionId, Address(20) + ":3074", emptyProfile:true, gatewaySentinel:true);
                Check(registry.TryCreate(new object(), host, LocalSessionCatalogueChecks.PublicBody(route:route), out uint id, out _), "four-slot synthetic map fixture");
                foreach (int n in new[] { 21, 22, 23, 24 })
                {
                    using var guest = RegisteredHostChecks.CreateRegistered(Address(n), network);
                    guest.TryCopyRegisteredPeer(out var guestIdentity);
                    using (guestIdentity)
                        Check(LocalSessionJoinCodec.TryReply(LocalSessionJoinChecks.Request(LocalSessionJoinChecks.Body(guestIdentity.ProfileId.ToArray(), id)),
                            registry, new object(), guest, out _, out _) == (n != 24), "three distinct pending guests respect original four-slot capacity");
                }
                Check(registry.TryCopyLiveSession(new object(), "invented-space"u8, id, out var snapshot), "multi guest snapshot");
                using (snapshot) Check(snapshot.PendingProfiles.Count == 3 && snapshot.ConfirmedProfiles.Count == 0,
                    "admission and reservation never fabricate successful game membership");
            }
        }
        // Generation cleanup uses the existing owner disposal path, never an
        // incoming malformed UDP packet to evict a currently admitted context.
        var endpoint = new IPEndPoint(Address(20), 12345);
        var contexts = new Dictionary<IPEndPoint, PrudpProbePeer> { [endpoint] = new() };
        var generations = new Dictionary<IPEndPoint, ulong> { [endpoint] = lobby.Generation(Address(20)) };
        var sessions = new Dictionary<IPEndPoint, PrudpLocalLoginSession>();
        Check(LocalConfigurationServer.ReclaimUnadmittedContexts(lobby, generations, sessions, contexts) == 0, "live context retained");
        int protocolOperations = 0;
        Check(LocalConfigurationServer.UseCurrentLobbyContext(lobby, endpoint, generations, sessions, contexts,
            () => ++protocolOperations) && protocolOperations == 1, "current generation may mutate protocol state");
        // This change occurs AFTER a successful maintenance pass but BEFORE the
        // next context operation, reproducing the former HTTP/UDP race exactly.
        Call(20, "leave"); Call(20, "admit");
        Check(!LocalConfigurationServer.UseCurrentLobbyContext(lobby, endpoint, generations, sessions, contexts,
            () => ++protocolOperations) && protocolOperations == 1 && contexts.Count == 0 && generations.Count == 0,
            "leave/re-admit after maintenance cannot process an old RPC or send");
        contexts[endpoint] = new(); generations[endpoint] = lobby.Generation(Address(20));
        Check(LocalConfigurationServer.UseCurrentLobbyContext(lobby, endpoint, generations, sessions, contexts,
            () => ++protocolOperations) && protocolOperations == 2, "fresh generation can process normally");
        ulong newGeneration = generations[endpoint];
        now += 15000;
        Check(!LocalConfigurationServer.UseCurrentLobbyContext(lobby, endpoint, generations, sessions, contexts,
            () => ++protocolOperations) && protocolOperations == 2 && contexts.Count == 0,
            "lease lapse before the next operation is enforced without waiting for timer maintenance");
        Check(Call(20, "admit").Status == 200 && lobby.Generation(Address(20)) != newGeneration, "new admission after lapse");
        Check(Call(10, "admit").Status == 200, "coordinator game lease start");
        var ownEndpoint = new IPEndPoint(Address(10), 12345);
        contexts[ownEndpoint] = new(); generations[ownEndpoint] = lobby.Generation(Address(10));
        contexts[endpoint] = new(); generations[endpoint] = lobby.Generation(Address(20));
        Call(10, "leave");
        Check(LocalConfigurationServer.ReclaimUnadmittedContexts(lobby, generations, sessions, contexts) == 1
            && contexts.ContainsKey(endpoint), "coordinator lease cleanup preserves other live owners");
        foreach (var context in contexts.Values) context.Dispose();

        // Real socket parsing, bounded on loopback, with only a synthetic policy.
        // Its public-control handler correctly rejects the actual loopback source.
        TcpListener listener = new(IPAddress.Loopback, 0); listener.Start();
        LocalNetworkPolicy httpPolicy = network;
        string lastReply = "";
        try
        {
            async Task<int> Http(string request)
            {
                using TcpClient client = new();
                var accept = listener.AcceptTcpClientAsync();
                await client.ConnectAsync((IPEndPoint)listener.LocalEndpoint);
                var server = await accept;
                Task serve = LocalConfigurationServer.ServeRequestAsync(server, [], httpPolicy, _ => { }, CancellationToken.None, () => 0);
                await client.GetStream().WriteAsync(Encoding.ASCII.GetBytes(request));
                using MemoryStream reply = new(); await client.GetStream().CopyToAsync(reply);
                await serve;
                lastReply = Encoding.ASCII.GetString(reply.ToArray());
                return int.Parse(lastReply.Split(' ')[1]);
            }
            string valid = Encoding.ASCII.GetString(Body());
            Check(await Http("POST /public/lobby/v1/admit HTTP/1.1\r\nContent-Length: " + valid.Length + "\r\n\r\n" + valid) == 403, "actual unadmitted loopback source denied");
            Check(await Http("POST /public/lobby/v1/admit HTTP/1.1\r\nContent-Length: 257\r\n\r\n") == 413, "oversize body rejected before reading");
            Check(await Http("POST /public/lobby/v1/admit HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n") == 400, "ambiguous content framing rejected");
            Check(await Http("POST /public/lobby/v1/admit HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n") == 400, "chunked control rejected");
            var ownAddress = System.Net.NetworkInformation.NetworkInterface.GetAllNetworkInterfaces()
                .Where(x => x.OperationalStatus == System.Net.NetworkInformation.OperationalStatus.Up)
                .SelectMany(x => x.GetIPProperties().UnicastAddresses).Select(x => x.Address)
                .FirstOrDefault(x => LocalNetworkPolicy.TryPrivateAddress(x.ToString(), out _));
            if (ownAddress != null)
            {
                // Self-connection to an existing local NIC only. No added NIC,
                // spoofed source, remote endpoint, game or firewall change.
                listener.Stop(); listener = new(ownAddress, 0); listener.Start();
                Check(LocalNetworkPolicy.TryCreateLobby(ownAddress.ToString(), Content, out httpPolicy), "actual selected NIC policy");
                Check(await Http("POST /public/lobby/v1/admit HTTP/1.1\r\nContent-Length: " + valid.Length + "\r\n\r\n" + valid) == 200, "real HTTP coordinator admission");
                Check(await Http("GET /public/lobby/v1/status HTTP/1.1\r\n\r\n") == 200 && lastReply.Contains("\"active_members\":1", StringComparison.Ordinal), "real HTTP reports active game lease");
                Check(await Http("GET /public/lobby/v1/peers HTTP/1.1\r\n\r\n") == 200, "real HTTP admitted directory");
                using (var actual = JsonDocument.Parse(lastReply.Split("\r\n\r\n", 2)[1]))
                    Check(actual.RootElement.GetProperty("peers").GetArrayLength() == 1, "coordinator never duplicated in peer list");
                Check(await Http("POST /public/lobby/v1/leave HTTP/1.1\r\nContent-Length: " + valid.Length + "\r\n\r\n" + valid) == 200, "real HTTP game leave");
                Check(await Http("GET /public/lobby/v1/status HTTP/1.1\r\n\r\n") == 200 && lastReply.Contains("\"active_members\":0", StringComparison.Ordinal), "real HTTP hub survives coordinator game leave");
                Check(await Http("GET /public/lobby/v1/peers HTTP/1.1\r\n\r\n") == 200, "reserved coordinator retains control access");
            }
        }
        finally { listener.Stop(); }
        return checks;
    }
}
