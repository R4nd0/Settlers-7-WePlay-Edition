using System.ComponentModel;
using System.Drawing.Drawing2D;
using System.Reflection;

namespace S7Desktop;

internal enum PlayMode { Solo, Lan, PrivateInternet, PublicInternet }

// One native button per card: the whole surface supports mouse, Tab, Space and
// Enter. The mode and features remain available to assistive technology.
internal sealed class PlayModeCard : Button
{
    public PlayMode Mode { get; }
    [DefaultValue("")] internal string Summary { get; set; } = "";
    [DefaultValue("")] internal string FeatureOne { get; set; } = "";
    [DefaultValue("")] internal string FeatureTwo { get; set; } = "";
    [DefaultValue("")] internal string ActionLabel { get; set; } = "";
    private bool hover;
    private bool compact;
    private readonly Color accent;
    private static readonly Image EasyTier = ReadImage("WePlay.EasyTier.png");
    private static readonly Image Discord = ReadImage("WePlay.Discord.png");

    public PlayModeCard(PlayMode mode)
    {
        Mode=mode;
        accent=mode switch {
            PlayMode.Solo=>Color.FromArgb(137,87,15),
            PlayMode.Lan=>Color.FromArgb(12,112,100),
            PlayMode.PrivateInternet=>Color.FromArgb(30,91,164),
            _=>Color.FromArgb(106,65,161)
        };
        Name="mode-"+mode;
        AccessibleRole=AccessibleRole.PushButton;
        Dock=DockStyle.Fill;Margin=new Padding(0,8,12,8);
        FlatStyle=FlatStyle.Flat;FlatAppearance.BorderSize=0;
        BackColor=Color.White;Cursor=Cursors.Hand;
        SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer,true);
        ResizeRedraw=true;TabStop=true;
        Height=S(282);MinimumSize=new Size(0,Height);
    }

    private static Image ReadImage(string name)
    {
        using var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)
            ?? throw new InvalidOperationException("Missing integration symbol: "+name);
        using var original=Image.FromStream(stream);
        return new Bitmap(original);
    }
    private int S(int value)=>Math.Max(1,(int)Math.Round(value*Font.Height/17f));
    internal void SetCompact(bool value)
    {
        if(compact==value)return;
        compact=value;
        Margin=new Padding(0,compact?4:8,12,compact?4:8);
        MinimumSize=new Size(0,S(compact?112:282));Height=MinimumSize.Height;Invalidate();
    }
    protected override void OnTextChanged(EventArgs e){base.OnTextChanged(e);AccessibleName=Text;Invalidate();}
    protected override void OnMouseEnter(EventArgs e){base.OnMouseEnter(e);hover=true;Invalidate();}
    protected override void OnMouseLeave(EventArgs e){base.OnMouseLeave(e);hover=false;Invalidate();}
    protected override void OnGotFocus(EventArgs e){base.OnGotFocus(e);Invalidate();}
    protected override void OnLostFocus(EventArgs e){base.OnLostFocus(e);Invalidate();}
    protected override void OnEnabledChanged(EventArgs e){base.OnEnabledChanged(e);Invalidate();}
    protected override void OnFontChanged(EventArgs e)
    {
        base.OnFontChanged(e);Height=S(compact?112:282);MinimumSize=new Size(0,Height);Invalidate();
    }
    public void RefreshDescription()
    {
        AccessibleDescription=string.Join(". ",Summary,FeatureOne,FeatureTwo,ActionLabel);
        AccessibleDefaultActionDescription=ActionLabel;
        Invalidate();
    }
    internal bool TextFits()
    {
        using var title=new Font(Font.FontFamily,Font.Size+(compact?1:3),FontStyle.Bold);
        using var action=new Font(Font,FontStyle.Bold);
        int width=Math.Max(1,Width-S(40));
        return TextRenderer.MeasureText(Text,title,new Size(Math.Max(1,Width-S(compact?76:111)),int.MaxValue),TextFormatFlags.WordBreak).Height<=S(compact?30:58)
            &&(compact||TextRenderer.MeasureText(Summary,Font,new Size(width,int.MaxValue),TextFormatFlags.WordBreak).Height<=S(56))
            &&new[]{FeatureOne,FeatureTwo}.All(t=>TextRenderer.MeasureText(t,Font).Width<=Width-S(compact?59:78))
            &&TextRenderer.MeasureText(ActionLabel,action).Width<=Width-S(60);
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        var g=e.Graphics;g.SmoothingMode=SmoothingMode.AntiAlias;
        bool contrast=SystemInformation.HighContrast;
        Color ink=Enabled?(contrast?SystemColors.WindowText:Color.FromArgb(27,38,54)):SystemColors.GrayText;
        Color mark=Enabled?(contrast?SystemColors.Highlight:accent):SystemColors.GrayText;
        Color surface=contrast?SystemColors.Window:(hover?Color.FromArgb(246,248,251):Color.White);
        using var fill=new SolidBrush(surface);g.FillRectangle(fill,ClientRectangle);
        using var border=new Pen(hover||Focused?mark:Color.FromArgb(210,218,228),Focused?S(2):1);
        g.DrawRectangle(border,1,1,Width-3,Height-3);
        using var stripe=new SolidBrush(mark);g.FillRectangle(stripe,1,1,Width-2,S(4));
        using var iconBack=new SolidBrush(contrast?SystemColors.Window:Color.FromArgb(242,245,249));
        g.FillEllipse(iconBack,S(compact?15:19),S(compact?7:22),S(compact?33:58),S(compact?33:58));
        ModeSymbol.Draw(g,new Rectangle(S(compact?16:21),S(compact?8:24),S(compact?31:54),S(compact?31:54)),Mode,mark);
        using var title=new Font(Font.FontFamily,Font.Size+(compact?1:3),FontStyle.Bold);
        TextRenderer.DrawText(g,Text,title,new Rectangle(S(compact?56:91),S(compact?7:25),Width-S(compact?76:111),S(compact?30:58)),ink,
            TextFormatFlags.WordBreak|TextFormatFlags.VerticalCenter|TextFormatFlags.NoPrefix);
        if(!compact)TextRenderer.DrawText(g,Summary,Font,new Rectangle(S(20),S(96),Width-S(40),S(56)),ink,
            TextFormatFlags.WordBreak|TextFormatFlags.NoPrefix);
        DrawFeature(g,FeatureOne,S(compact?39:163),Mode is PlayMode.PrivateInternet or PlayMode.PublicInternet?EasyTier:null,ink,mark);
        DrawFeature(g,FeatureTwo,S(compact?61:200),Mode==PlayMode.PublicInternet?Discord:null,ink,mark);
        using var line=new Pen(contrast?SystemColors.WindowText:Color.FromArgb(229,234,240));
        g.DrawLine(line,S(20),Height-S(compact?29:48),Width-S(20),Height-S(compact?29:48));
        using var actionFont=new Font(Font,FontStyle.Bold);
        TextRenderer.DrawText(g,ActionLabel,actionFont,new Rectangle(S(20),Height-S(compact?27:37),Width-S(60),S(compact?24:26)),mark,
            TextFormatFlags.VerticalCenter|TextFormatFlags.NoPrefix);
        using var arrow=new Pen(mark,S(2)){StartCap=LineCap.Round,EndCap=LineCap.Round};
        int x=Width-S(29),y=Height-S(compact?15:24);
        g.DrawLine(arrow,x-S(10),y,x+S(6),y);g.DrawLine(arrow,x,y-S(6),x+S(6),y);g.DrawLine(arrow,x,y+S(6),x+S(6),y);
        if(Focused&&ShowFocusCues)ControlPaint.DrawFocusRectangle(g,new Rectangle(S(7),S(9),Width-S(14),Height-S(16)),ink,surface);
    }
    private void DrawFeature(Graphics g,string text,int top,Image? logo,Color ink,Color mark)
    {
        if(logo!=null)
        {
            // Keep official proportions and colors, with clear space around it.
            int size=S(compact?18:25);
            float ratio=Math.Min((float)size/logo.Width,(float)size/logo.Height);
            float w=logo.Width*ratio,h=logo.Height*ratio;
            g.DrawImage(logo,S(20)+(size-w)/2,top+(size-h)/2,w,h);
        }
        else
        {
            var state=g.Save();
            if(compact){g.TranslateTransform(S(5),top*.28f);g.ScaleTransform(.72f,.72f);}
            using var pen=new Pen(mark,S(2)){StartCap=LineCap.Round,EndCap=LineCap.Round};
            if(Mode==PlayMode.PrivateInternet)
            {
                g.DrawRectangle(pen,S(22),top+S(5),S(22),S(15));
                g.DrawLine(pen,S(22),top+S(5),S(33),top+S(13));g.DrawLine(pen,S(44),top+S(5),S(33),top+S(13));
            }
            else
            {
                g.DrawEllipse(pen,S(23),top+S(4),S(19),S(19));
                g.DrawLine(pen,S(28),top+S(13),S(32),top+S(17));g.DrawLine(pen,S(32),top+S(17),S(38),top+S(10));
            }
            g.Restore(state);
        }
        TextRenderer.DrawText(g,text,Font,new Rectangle(S(compact?43:58),top,Width-S(compact?59:78),S(compact?21:27)),ink,
            TextFormatFlags.VerticalCenter|TextFormatFlags.NoPrefix);
    }
}

internal static class ModeSymbol
{
    public static void Draw(Graphics graphics,Rectangle bounds,PlayMode mode,Color color)
    {
        var saved=graphics.Save();graphics.TranslateTransform(bounds.X,bounds.Y);graphics.ScaleTransform(bounds.Width/64f,bounds.Height/64f);
        using var pen=new Pen(color,3){StartCap=LineCap.Round,EndCap=LineCap.Round,LineJoin=LineJoin.Round};
        if(mode==PlayMode.Solo)
        {
            graphics.DrawEllipse(pen,23,8,18,18);
            graphics.DrawArc(pen,12,30,40,39,180,180);graphics.DrawLine(pen,12,50,52,50);
        }
        else if(mode==PlayMode.Lan)
        {
            foreach(int x in new[]{4,36})
            {graphics.DrawRectangle(pen,x,10,24,19);graphics.DrawLine(pen,x+12,29,x+12,36);graphics.DrawLine(pen,x+5,36,x+19,36);}
            graphics.DrawLine(pen,16,42,16,49);graphics.DrawLine(pen,16,49,48,49);graphics.DrawLine(pen,48,49,48,42);
            graphics.DrawLine(pen,32,49,32,57);graphics.DrawEllipse(pen,28,55,8,6);
        }
        else
        {
            graphics.DrawEllipse(pen,7,5,44,44);graphics.DrawEllipse(pen,20,5,18,44);
            graphics.DrawLine(pen,8,27,50,27);graphics.DrawArc(pen,8,11,42,16,0,180);
            if(mode==PlayMode.PrivateInternet)
            {
                using var back=new SolidBrush(SystemColors.Window);graphics.FillRectangle(back,34,34,27,28);
                graphics.DrawArc(pen,39,28,16,22,180,180);graphics.DrawRectangle(pen,35,39,24,20);
                graphics.DrawLine(pen,47,47,47,52);
            }
            else
            {
                using var back=new SolidBrush(SystemColors.Window);graphics.FillRectangle(back,9,37,51,25);
                foreach(int x in new[]{17,43})
                {graphics.DrawEllipse(pen,x,38,10,10);graphics.DrawArc(pen,x-5,51,20,20,180,180);}
            }
        }
        graphics.Restore(saved);
    }
}
