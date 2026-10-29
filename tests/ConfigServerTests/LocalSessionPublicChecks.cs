using System.Buffers.Binary;
using System.Net;
using System.Text;
using System.Text.Json;
using S7Patcher.Source;

internal static class LocalSessionPublicChecks
{
    internal static int Run()
    {
        int checks=0;
        void Check(bool value,string label){++checks;if(!value)throw new Exception(label);}
        var address=IPAddress.Parse("192.168.10.20");
        LocalNetworkPolicy.TryCreateLobby("192.168.10.10",new string('a',64),out var network);
        byte[] admission=Encoding.UTF8.GetBytes("{\"format\":\"s7-lobby-control-v1\",\"content_id\":\""+new string('a',64)+"\"}");
        Check(network.Lobby.Handle(address,"POST",LocalLobbyAdmission.Prefix+"admit",admission,0).Status==200,"admit host");
        using var host=RegisteredHostChecks.CreateRegistered(address,network);
        using var registry=new LocalSessionRegistry(catalogueProbe:true);object owner=new();
        Check(registry.TryCreate(owner,host,LocalSessionCatalogueChecks.PublicBody(),out uint id,out _),"public setup");
        JsonDocument Read()=>JsonDocument.Parse(registry.PublicGamesFor(address));
        using(var games=Read())
        {
            var game=games.RootElement[0];Check(games.RootElement.GetArrayLength()==1,"one own game");
            Check(game.GetProperty("state").GetString()=="open" && game.GetProperty("free_slots").GetInt32()==3,"open slots");
            Check(game.EnumerateObject().Count()==6 && !games.RootElement.GetRawText().Contains(address.ToString()),"only public fields");
        }
        using(var others=JsonDocument.Parse(registry.PublicGamesFor(IPAddress.Parse("192.168.10.21"))))
            Check(others.RootElement.GetArrayLength()==0,"no other owner's game");
        byte[] Number(string key,long value)
        {byte[] encoded=new byte[8];BinaryPrimitives.WriteInt64LittleEndian(encoded,value);return [..LocalSessionSearchChecks.Ddl(key),6,..encoded];}
        void Update(params byte[][] fields)=>Check(registry.TryUpdate(owner,LocalSessionUpdateChecks.Body(id,
            attributes:LocalSessionUpdateChecks.Map(fields)),out _),"owner update");
        Update(Number("settlers_free_slots",0));
        using(var games=Read())Check(games.RootElement[0].GetProperty("state").GetString()=="full","full is not started");
        Update(Number("settlers_max_slots",0));
        using(var games=Read())Check(games.RootElement[0].GetProperty("state").GetString()=="started" &&
            games.RootElement[0].GetProperty("capacity").GetInt32()==4,"start signal retains map capacity");
        Update([..LocalSessionSearchChecks.Ddl("settlers_session_name"),4,..LocalSessionSearchChecks.Ddl("weplay1.private-material")]);
        using(var games=Read())Check(games.RootElement[0].GetProperty("title").GetString()=="Siedler 7","unsafe title masked");
        Update([..LocalSessionSearchChecks.Ddl("private"),3,1]);
        using(var games=Read())Check(games.RootElement.GetArrayLength()==0,"private session omitted");
        Update([..LocalSessionSearchChecks.Ddl("private"),3,0]);
        registry.RemoveOwner(owner);
        using(var games=Read())Check(games.RootElement.GetArrayLength()==0,"closed owner removed");
        using var empty=new LocalSessionRegistry();
        Check(empty.TryCreate(new(),host,LocalSessionCreationChecks.Body(),out _,out _),"opaque setup");
        using(var games=JsonDocument.Parse(empty.PublicGamesFor(address)))Check(games.RootElement.GetArrayLength()==0,"unknown metadata not fabricated");
        var lobby=network.Lobby;
        var stranger=IPAddress.Parse("192.168.10.21");
        Check(lobby.Handle(stranger,"GET",LocalLobbyAdmission.Prefix+"games",[],0,registry.PublicGamesFor).Status==403,"unadmitted viewer refused");
        Check(lobby.Handle(stranger,"POST",LocalLobbyAdmission.Prefix+"admit",admission,0).Status==200,"admit viewer");
        var response=lobby.Handle(address,"GET",LocalLobbyAdmission.Prefix+"games",[],0,registry.PublicGamesFor);
        using(var games=JsonDocument.Parse(response.Body))Check(response.Status==200 &&
            games.RootElement.GetProperty("format").GetString()=="s7-lobby-games-v1" &&
            games.RootElement.GetProperty("content_id").GetString()==new string('a',64),"content-bound projection");
        Check(lobby.Handle(address,"GET",LocalLobbyAdmission.Prefix+"games",[],0).Status==503,"absent producer unavailable");
        return checks;
    }
}
