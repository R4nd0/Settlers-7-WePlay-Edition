using System.Drawing.Drawing2D;
using System.Reflection;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private readonly Image discordSymbol=CommunitySymbol("WePlay.Discord.png");
    private readonly Image coffeeSymbol=CommunitySymbol("WePlay.Coffee.png");

    private static Image CommunitySymbol(string resource)
    {
        using var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream(resource)
            ?? throw new InvalidOperationException("Missing community symbol: "+resource);
        using var source=Image.FromStream(stream);
        var result=new Bitmap(22,22);
        using var graphics=Graphics.FromImage(result);
        graphics.InterpolationMode=InterpolationMode.HighQualityBicubic;
        float scale=Math.Min(22f/source.Width,22f/source.Height);
        float width=source.Width*scale,height=source.Height*scale;
        graphics.DrawImage(source,(22-width)/2,(22-height)/2,width,height);
        return result;
    }

    private Button OpenDiscordButton(string key)
    {
        var button = Button(key, () =>
        {
            Community.OpenDiscord();
            return Task.CompletedTask;
        });
        button.Image = discordSymbol;
        button.TextImageRelation = TextImageRelation.ImageBeforeText;
        return button;
    }
}
