namespace S7Desktop;

internal sealed partial class MainForm
{
    private bool ConfirmInternetConnection(bool publicLobby)
    {
        var consent=BuildInternetConsent(publicLobby);
        using var dialog=consent.Dialog;
        return dialog.ShowDialog(this)==DialogResult.OK&&consent.Choice.Checked;
    }

    private static (Form Dialog,CheckBox Choice,Button Connect) BuildInternetConsent(bool publicLobby)
    {
        var dialog=new Form {
            Text=Lang.T("internetConsentTitle"),StartPosition=FormStartPosition.CenterParent,
            ClientSize=new Size(720,590),MinimumSize=new Size(620,480),MaximizeBox=false,MinimizeBox=false
        };
        var layout=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=1,RowCount=3,Padding=new Padding(20)};
        layout.RowStyles.Add(new RowStyle(SizeType.Percent,100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var body=new Panel {Dock=DockStyle.Fill,AutoScroll=true};
        var explanation=new Label {AutoSize=true,Dock=DockStyle.Top,MaximumSize=new Size(650,0),
            Text=Lang.T(publicLobby?"internetConsentPublic":"internetConsentPrivate")+"\n\n"+Lang.T("internetConsentDetails")};
        body.Controls.Add(explanation);
        var choice=new CheckBox {AutoSize=true,Checked=false,MaximumSize=new Size(650,0),Margin=new Padding(0,16,0,12),
            Text=Lang.T("internetConsentCheck"),AccessibleName=Lang.T("internetConsentCheck")};
        var connect=new Button {AutoSize=true,MinimumSize=new Size(220,40),Enabled=false,Text=Lang.T("internetConsentConnect"),DialogResult=DialogResult.OK};
        var cancel=new Button {AutoSize=true,MinimumSize=new Size(130,40),Text=Lang.T("cancel"),DialogResult=DialogResult.Cancel};
        var buttons=new FlowLayoutPanel {AutoSize=true,Dock=DockStyle.Fill,FlowDirection=FlowDirection.RightToLeft};
        buttons.Controls.Add(connect);buttons.Controls.Add(cancel);
        choice.CheckedChanged+=(_,_)=>connect.Enabled=choice.Checked;
        layout.Controls.Add(body,0,0);layout.Controls.Add(choice,0,1);layout.Controls.Add(buttons,0,2);
        dialog.Controls.Add(layout);dialog.CancelButton=cancel;dialog.AcceptButton=connect;
        var symbol=CommunitySymbol("WePlay.Discord.png");
        ConfigureDiscordSymbols(dialog,symbol);dialog.Disposed+=(_,_)=>symbol.Dispose();
        dialog.Shown+=(_,_)=>cancel.Focus();
        return (dialog,choice,connect);
    }
}
