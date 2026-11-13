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

    private static void ConfigureDiscordSymbols(Control root,Image symbol)
    {
        // Follow the actual translated text, including changing connection and
        // status messages. Editable fields and copied invitations stay plain text.
        foreach(var control in Descendants(root))
        {
            if(control is PlayModeCard || (control is not System.Windows.Forms.Label && control is not ButtonBase))continue;
            // The BMAC link has its own symbol and is never a Discord control.
            if(control.AccessibleDescription==Community.CoffeeUrl)continue;
            Padding original=control.Padding;
            bool show=false;
            if(control is System.Windows.Forms.Label caption)
                caption.Paint+=(_,args)=>
                {
                    if(!show)return;
                    int y=caption.TextAlign is ContentAlignment.MiddleLeft or ContentAlignment.MiddleRight
                        ?(caption.Height-symbol.Height)/2:original.Top;
                    // Label.Image uses the same padded rectangle as its text.
                    // Paint in the reserved gutter so multiline text cannot overlap.
                    args.Graphics.DrawImage(symbol,original.Left,y);
                };
            void RefreshSymbol()
            {
                show=control.Text.Contains("Discord",StringComparison.OrdinalIgnoreCase);
                if(control is System.Windows.Forms.Label label)
                {
                    label.Padding=show?new Padding(original.Left+30,original.Top,original.Right,original.Bottom):original;
                    label.Invalidate();
                }
                else if(control is ButtonBase button)
                {
                    button.Image=show?symbol:null;
                    button.TextImageRelation=TextImageRelation.ImageBeforeText;
                    if(button is CheckBox)button.ImageAlign=ContentAlignment.MiddleLeft;
                }
            }
            control.TextChanged+=(_,_)=>RefreshSymbol();
            RefreshSymbol();
        }
    }
}
