using System.Diagnostics;
using System.Text.RegularExpressions;

namespace S7Desktop;

internal static class Community
{
    public const string DiscordCode = "j2eQgVzssc";
    public const string DiscordUrl = "https://discord.gg/" + DiscordCode;
    public static void OpenDiscord()
    {
        try { Process.Start(new ProcessStartInfo(DiscordUrl) { UseShellExecute=true }); }
        catch(Exception error) when(error is System.ComponentModel.Win32Exception or InvalidOperationException)
        { throw new BackendError("discordOpenFailed", ""); }
    }

    // This is a deliberate clipboard export. It never opens a URL containing
    // the room secret, posts a Discord message or places it on a command line.
    public static string InvitationMessage(string invitation, string language)
    {
        string code = InvitationCode(invitation);
        if(language=="de")
            return code+"\n\nWePlay – private Raumeinladung\n"+
                "Im WePlay-Starter: Internet spielen → Ich habe eine Einladung → Einladung einfügen → Raum beitreten + S7 starten.\n"+
                "Danach in S7: Mehrspieler → Partie des Erstellers auswählen und beitreten.\n"+
                "Alle benötigen dieselbe WePlay-Version. Der Ersteller muss verbunden bleiben.\n"+
                "Teile diesen Code nur mit deinen Mitspielern.\nCommunity: "+DiscordUrl;
        return code+"\n\nWePlay – private room invitation\n"+
            "In the WePlay launcher: Internet play → I have an invitation → Paste invitation → Join room + start S7.\n"+
            "Then in S7: Multiplayer → select the creator's match and join it.\n"+
            "Everyone needs the same WePlay version. The creator must stay connected.\n"+
            "Share this code only with your players.\nCommunity: "+DiscordUrl;
    }

    public static string InvitationCode(string text)
    {
        // Accept either a raw invitation or the exact share message generated
        // above. Do not scan arbitrary links/messages for embedded secrets.
        if(text.Length>4096)throw new BackendError("internet_invitation_invalid", "");
        string[] lines=text.Trim().Replace("\r\n","\n").Split('\n');
        string code=lines[0];
        if(code.Length>2048||!Regex.IsMatch(code,@"\Aweplay1\.[A-Za-z0-9_-]+\z") ||
            (lines.Length>1 && !(lines.Length>=3 && lines[1]=="" &&
                lines[2] is "WePlay – private Raumeinladung" or "WePlay – private room invitation")))
            throw new BackendError("internet_invitation_invalid", "");
        return code;
    }
}
