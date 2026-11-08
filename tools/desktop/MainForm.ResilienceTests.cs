using System.Text.Json;

namespace S7Desktop;

internal sealed partial class MainForm
{
    private static void AwaitUi(Task task)
    {
        var watch=System.Diagnostics.Stopwatch.StartNew();
        while(!task.IsCompleted&&watch.Elapsed<TimeSpan.FromSeconds(5)){Application.DoEvents();Thread.Sleep(1);}
        if(!task.IsCompleted)throw new Exception("UI fixture exceeded its bounded completion time");
        task.GetAwaiter().GetResult();
    }
    private void TestLauncherResilience(JsonElement original)
    {
        LoadSnapshot(original,acceptGraphics:true);
        string saved=CaptureGraphicsDraft();qualities["TextureQuality"].SelectedIndex=2;
        if(!GraphicsDirty||!graphicsDiscard.Enabled)throw new Exception("Graphics draft is not marked unsaved");
        string draft=CaptureGraphicsDraft();LoadSnapshot(original);
        if(CaptureGraphicsDraft()!=draft||!GraphicsDirty)throw new Exception("Hardware snapshot overwrites an unsaved graphics draft");
        bool allowed=false;AwaitUi(ResolveGraphicsDraft(DialogResult.Cancel).ContinueWith(t=>allowed=t.Result));
        if(allowed||CaptureGraphicsDraft()!=draft)throw new Exception("Cancel discards the graphics draft");
        player.Text="Draft player";AwaitUi(ResolveGraphicsDraft(DialogResult.No));
        if(GraphicsDirty||CaptureGraphicsDraft()!=saved)throw new Exception("Discard does not restore saved graphics values");
        if(player.Text!="Draft player")throw new Exception("Discarding graphics overwrites unrelated name edits");
        qualities["TextureQuality"].SelectedIndex=2;int saves=0;
        AwaitUi(ResolveGraphicsDraft(DialogResult.Yes,()=>{saves++;AcceptGraphicsDraft();return Task.CompletedTask;}));
        if(saves!=1||GraphicsDirty)throw new Exception("Save decision does not commit the graphics draft");
        LoadSnapshot(original,acceptGraphics:true);

        string previous=Lang.Current;changingLanguage=true;language.SelectedIndex=previous=="de"?1:0;changingLanguage=false;
        ChangeLanguage((value,_)=>{Lang.Set(value,false);throw new IOException("Synthetic language persistence failure");});
        if(Lang.Current!=previous||language.SelectedIndex!=(previous=="de"?0:1))throw new Exception("Failed language persistence changes the selection");
        cancelRequested=false;RequestCancellation(()=>throw new IOException("Synthetic cancellation request failure"));
        if(cancelRequested||status.Text!=Lang.T("cancel_request_failed"))throw new Exception("Failed cancellation is shown as requested");
        lastError="";details.Clear();status.Text=Lang.T("ready");

        directoryAvailable=true;directoryConnected=true;tabs.SelectedTab=directoryPage;
        using var listed=JsonDocument.Parse("""{"connected":true,"games":[{"id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","title":"UI fixture game","compatible":true,"state":"open","free_slots":3,"players":1,"capacity":4,"version":"UI fixture"}]}""");
        ShowDirectory(listed.RootElement,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");Application.DoEvents();
        var completion=new TaskCompletionSource<JsonElement>();
        Task refresh=RefreshDirectoryCore(true,_=>completion.Task);
        if(busy||!tabs.Enabled||directoryRows.Count!=1||directoryJoin.Enabled||!directoryRefreshing)
            throw new Exception("Background refresh locks navigation, clears rows or permits stale joining");
        using var obsolete=JsonDocument.Parse(listed.RootElement.GetRawText().Replace("UI fixture game","Obsolete result"));
        SetBusy(true);SetBusy(false);completion.SetResult(obsolete.RootElement.Clone());AwaitUi(refresh);
        if(directoryRefreshing||directoryRows.Count!=1||directoryRows[0].GetProperty("title").GetString()!="UI fixture game"||directoryJoin.Enabled)
            throw new Exception("Obsolete refresh overwrites current rows or restores joining without fresh data");
        var failed=RefreshDirectoryCore(true,_=>Task.FromException<JsonElement>(new IOException("Synthetic list failure")));AwaitUi(failed);
        if(!directoryLoadFailed||directoryRows.Count!=1||directoryJoin.Enabled)throw new Exception("Failed refresh loses stale rows or enables joining");
        AwaitUi(RefreshDirectoryCore(true,_=>Task.FromResult(listed.RootElement.Clone())));
        if(directoryLoadFailed||directoryRefreshing||!directoryJoin.Enabled)throw new Exception("Successful refresh does not restore safe joining");
        ShowDirectory(listed.RootElement,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        LoadSnapshot(original,acceptGraphics:true);tabs.SelectedTab=homePage;
        maintenanceCount=2;UpdateMaintenanceNotice();
        if(!maintenanceNotice.Visible||!maintenanceNotice.Text.Contains('2'))throw new Exception("Pending maintenance has no reachable launcher notice");
        maintenanceCount=0;UpdateMaintenanceNotice();
    }
    private void AssertPrimaryActionVisible(TabPage page)
    {
        Control[] actions=page==internetPage?[internetHostChoice.Checked?internetCreate:internetJoinButton]:
            page==lanPage?lanActionBar.Controls.Cast<Control>().ToArray():
            page==directoryPage?[directoryJoin]:page==graphicsPage?[graphicsSave]:[];
        foreach(var action in actions)
        {
            if(!action.Visible)throw new Exception("Primary action hidden on "+page.Text);
            Rectangle bounds=page.RectangleToClient(action.RectangleToScreen(action.ClientRectangle));
            if(!page.ClientRectangle.Contains(bounds))throw new Exception("Primary action requires scrolling on "+page.Text);
            var hit=page.GetChildAtPoint(new Point(bounds.Left+bounds.Width/2,bounds.Top+bounds.Height/2),GetChildAtPointSkip.Invisible);
            if(hit==null||(hit!=action&&!hit.Contains(action)))throw new Exception("Primary action is covered by another panel on "+page.Text);
        }
    }
    private static void TestPrimaryActionScaling(JsonElement fixture,string folder,string locale)
    {
        // Deterministic 125/150% geometry and text scaling. This supplements the
        // current monitor render; it does not claim a physical multi-monitor test.
        foreach(float scale in new[]{1.25f,1.5f})
        {
            using var form=new MainForm(test:true) {StartPosition=FormStartPosition.Manual,Location=new Point(-30000,-30000),ShowInTaskbar=false};
            var fonts=Descendants(form).Prepend(form).Select(control=>(Control:control,Font:control.Font)).ToArray();
            form.Scale(new SizeF(scale,scale));
            var ownedFonts=new List<Font>();
            try
            {
                foreach(var item in fonts){var font=new Font(item.Font.FontFamily,item.Font.Size*scale,item.Font.Style);ownedFonts.Add(font);item.Control.Font=font;}
                form.LoadSnapshot(fixture,acceptGraphics:true);form.ShowMain();form.automaticMenuHeight=false;
                form.Size=form.MinimumSize;form.Show();Application.DoEvents();
                foreach(var page in new[]{form.lanPage,form.internetPage,form.directoryPage,form.graphicsPage})
                {
                    form.tabs.SelectedTab=page;Application.DoEvents();form.AssertPrimaryActionVisible(page);
                    if(page==form.internetPage||page==form.graphicsPage)
                    {
                        using var render=new Bitmap(form.Width,form.Height);form.DrawToBitmap(render,new Rectangle(0,0,form.Width,form.Height));
                        render.Save(Path.Combine(folder,locale+"-scaled-"+(int)(scale*100)+"-"+form.tabs.SelectedIndex+".png"));
                    }
                }
                form.DiscardGraphicsDraft();form.Close();
            }
            finally{foreach(var font in ownedFonts)font.Dispose();}
        }
    }
}
