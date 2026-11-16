namespace S7Desktop;

internal sealed partial class MainForm
{
    private bool crossplayAvailable;
    private readonly ComboBox internetSessionKind=Combo(),crossplayHistory=Combo();
    private readonly Label crossplayNotice=new() {AutoSize=true,MaximumSize=new Size(850,0)};
    private Control crossplayKindField=null!,crossplayHistoryField=null!;
    private bool MixedInternet=>crossplayAvailable&&internetSessionKind.SelectedIndex==1;

    private void BuildCrossplayControls(Action<Control> add)
    {
        crossplayKindField=Field("crossplaySessionKind",internetSessionKind);add(crossplayKindField);
        crossplayHistoryField=Field("crossplayHistoryEdition",crossplayHistory);add(crossplayHistoryField);
        Bind(crossplayNotice,"crossplayScope");add(crossplayNotice);
        internetSessionKind.SelectedIndexChanged+=(_,_)=>{UpdateInternetControls();FitMenuHeight();};
        crossplayHistory.SelectedIndexChanged+=(_,_)=>UpdateInternetControls();
        ApplyCrossplayLanguage();
    }

    private void ApplyCrossplayLanguage()
    {
        int kind=internetSessionKind.SelectedIndex,edition=crossplayHistory.SelectedIndex;
        internetSessionKind.Items.Clear();internetSessionKind.Items.AddRange([Lang.T("crossplayHistoryOnly"),Lang.T("crossplayTitle")]);
        internetSessionKind.SelectedIndex=Math.Max(0,kind);
        crossplayHistory.Items.Clear();crossplayHistory.Items.AddRange([Lang.T("crossplayUbisoft"),Lang.T("crossplaySteam")]);
        crossplayHistory.SelectedIndex=edition;
    }

    private void UpdateCrossplayControls()
    {
        if(crossplayKindField==null)return;
        if(!crossplayAvailable&&internetSessionKind.SelectedIndex!=0)internetSessionKind.SelectedIndex=0;
        if(singlePlayerOnly&&crossplayAvailable&&internetSessionKind.SelectedIndex!=1)internetSessionKind.SelectedIndex=1;
        bool mixed=MixedInternet;
        if(mixed)
        {
            if(singlePlayerOnly&&!internetHostChoice.Checked)internetHostChoice.Checked=true;
            if(!singlePlayerOnly&&!internetGuestChoice.Checked)internetGuestChoice.Checked=true;
        }
        crossplayKindField.Visible=crossplayAvailable&&!singlePlayerOnly&&!internetOperation;
        crossplayHistoryField.Visible=mixed&&singlePlayerOnly&&!internetOperation;
        crossplayNotice.Visible=mixed&&!internetOperation;
        internetSessionKind.Enabled=crossplayHistory.Enabled=!busy;
        internetHostChoice.Visible=!internetOperation&&(!mixed||singlePlayerOnly);
        internetGuestChoice.Visible=!internetOperation&&(!mixed||!singlePlayerOnly);
        if(mixed&&singlePlayerOnly&&crossplayHistory.SelectedIndex<0)internetCreate.Enabled=false;
        foreach(var label in labels.Where(x=>x.Value is "internetHostSteps" or "internetGuestSteps"))
            label.Key.Text=Lang.T(mixed?(label.Value=="internetHostSteps"?"crossplayHostSteps":"crossplayGuestSteps"):label.Value);
        if(mixed&&internetOperation)
            internetNext.Text=Lang.T(internetCreating?"crossplayHostLive":"crossplayGuestLive");
    }
}
