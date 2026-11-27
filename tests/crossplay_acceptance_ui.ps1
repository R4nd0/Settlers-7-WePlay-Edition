# Windows desktop adapter for the isolated game windows owned by the acceptance runner.
param([switch]$SelfTest)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName System.Drawing, System.Runtime.WindowsRuntime
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AcceptanceWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr wnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr wnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr wnd, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr wnd, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr wnd, uint message, IntPtr arg1, IntPtr arg2);
    [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
    [StructLayout(LayoutKind.Sequential)] struct Mouse { public int X, Y; public uint Data, Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Sequential)] struct Key { public ushort Code, Scan; public uint Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Explicit)] struct InputData {
        [FieldOffset(0)] public Mouse Mouse;
        [FieldOffset(0)] public Key Key;
    }
    [StructLayout(LayoutKind.Sequential)] struct Input { public uint Type; public InputData Data; }
    [DllImport("user32.dll")] static extern uint SendInput(uint count, Input[] inputs, int size);
    public static bool Click() {
        var events = new Input[2];
        events[0].Data.Mouse.Flags = 2;
        events[1].Data.Mouse.Flags = 4;
        return SendInput(2, events, Marshal.SizeOf(typeof(Input))) == 2;
    }
    public static bool Press(ushort key) {
        var events = new Input[2];
        events[0].Type = events[1].Type = 1;
        events[0].Data.Key.Code = events[1].Data.Key.Code = key;
        events[1].Data.Key.Flags = 2;
        return SendInput(2, events, Marshal.SizeOf(typeof(Input))) == 2;
    }
}
'@
[void][AcceptanceWindow]::SetProcessDPIAware()
$engine = [Windows.Media.Ocr.OcrEngine,Windows.Foundation,ContentType=WindowsRuntime]::TryCreateFromUserProfileLanguages()
if ($null -eq $engine) { throw 'Install Windows OCR for one of your display languages.' }
$null = [Windows.Storage.Streams.InMemoryRandomAccessStream,Windows.Storage.Streams,ContentType=WindowsRuntime]
$null = [Windows.Storage.Streams.DataWriter,Windows.Storage.Streams,ContentType=WindowsRuntime]
$null = [Windows.Graphics.Imaging.BitmapDecoder,Windows.Graphics.Imaging,ContentType=WindowsRuntime]
$null = [Windows.Graphics.Imaging.SoftwareBitmap,Windows.Graphics.Imaging,ContentType=WindowsRuntime]
$null = [Windows.Media.Ocr.OcrResult,Windows.Foundation,ContentType=WindowsRuntime]
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1

function Await($operation, [type]$type) {
    $task = $asTask.MakeGenericMethod($type).Invoke($null, @($operation))
    if (!$task.Wait(4000)) { throw 'Windows OCR timed out.' }
    return $task.Result
}

function ReadText([Drawing.Bitmap]$image) {
    $bytes = [IO.MemoryStream]::new()
    $stream = [Windows.Storage.Streams.InMemoryRandomAccessStream]::new()
    $writer = [Windows.Storage.Streams.DataWriter]::new($stream)
    try {
        $image.Save($bytes, [Drawing.Imaging.ImageFormat]::Png)
        $writer.WriteBytes($bytes.ToArray())
        $null = Await ($writer.StoreAsync()) ([uint32])
        $null = $writer.DetachStream()
        $stream.Seek(0)
        $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
        $bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
        try { return (Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])).Text }
        finally { $bitmap.Dispose() }
    } finally {
        $writer.Dispose()
        $stream.Dispose()
        $bytes.Dispose()
    }
}

if ($SelfTest) {
    $image = [Drawing.Bitmap]::new(500, 100)
    $draw = [Drawing.Graphics]::FromImage($image)
    $font = [Drawing.Font]::new('Segoe UI', 28)
    try {
        $draw.Clear([Drawing.Color]::White)
        $draw.DrawString('Ready Bereit', $font, [Drawing.Brushes]::Black, 8, 8)
        if ((ReadText $image) -notmatch 'Ready\s+Bereit') { throw 'Windows OCR fixture did not recognize the test text.' }
        'Windows OCR fixture passed.'
    } finally { $font.Dispose(); $draw.Dispose(); $image.Dispose() }
    exit
}

function Handle($request) {
    $game = Get-Process -Id $request.game_pid
    if ($game.ProcessName -ne 'Settlers7R' -or $game.Path -ne $request.image) { throw 'Game process no longer matches the test copy.' }
    $window = $game.MainWindowHandle
    if ($window -eq [IntPtr]::Zero -or $game.MainWindowTitle -ne 'Settlers 7 Window') { throw 'The game window is not ready.' }
    if ($request.action -eq 'close') {
        if (![AcceptanceWindow]::PostMessage($window, 16, [IntPtr]::Zero, [IntPtr]::Zero)) { throw 'Could not close the game window.' }
        return @{}
    }
    if ($request.action -in @('focus', 'read')) {
        if ([AcceptanceWindow]::GetForegroundWindow() -ne $window) {
            [void][AcceptanceWindow]::SetForegroundWindow($window)
            if ([AcceptanceWindow]::GetForegroundWindow() -ne $window) {
                $shell = New-Object -ComObject WScript.Shell
                try { [void]$shell.AppActivate($game.Id) }
                finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell) }
            }
            Start-Sleep -Milliseconds 100
        }
    }
    if ([AcceptanceWindow]::GetForegroundWindow() -ne $window) { throw 'The test copy does not have foreground focus.' }
    $rect = New-Object AcceptanceWindow+Rect
    $origin = New-Object AcceptanceWindow+Point
    if (![AcceptanceWindow]::GetClientRect($window, [ref]$rect) -or $rect.R -ne 1024 -or $rect.B -ne 768 -or
        ![AcceptanceWindow]::ClientToScreen($window, [ref]$origin)) { throw 'Expected a 1024 by 768 game client in windowed mode.' }
    if ($request.action -eq 'focus') { return @{} }
    if ($request.action -eq 'read') {
        $box = $request.box
        $image = [Drawing.Bitmap]::new($box[2], $box[3]); $draw = [Drawing.Graphics]::FromImage($image)
        try {
            $draw.CopyFromScreen(($origin.X + $box[0]), ($origin.Y + $box[1]), 0, 0, $image.Size)
            if ([AcceptanceWindow]::GetForegroundWindow() -ne $window) { throw 'Focus changed during the capture.' }
            return @{text = (ReadText $image)}
        } finally { $draw.Dispose(); $image.Dispose() }
    }
    if (@(1, 2, 4, 16, 17, 18, 91, 92) | Where-Object { [AcceptanceWindow]::GetAsyncKeyState($_) -lt 0 }) {
        throw 'Mouse button or modifier held during test input.'
    }
    if ($request.action -eq 'click') {
        $point = New-Object AcceptanceWindow+Point
        $point.X = $origin.X + $request.x; $point.Y = $origin.Y + $request.y
        if ([AcceptanceWindow]::GetAncestor([AcceptanceWindow]::WindowFromPoint($point), 2) -ne $window -or
            ![AcceptanceWindow]::SetCursorPos($point.X, $point.Y)) { throw 'The test button is covered by another window.' }
        if ([AcceptanceWindow]::GetForegroundWindow() -ne $window -or ![AcceptanceWindow]::Click()) {
            throw 'Windows did not deliver the test click.'
        }
    } elseif ($request.action -in @('escape', 'type')) {
        if ($request.action -eq 'escape') { $keys = @(27) }
        elseif ($request.text -cmatch '^[a-z0-9]{1,24}$') { $keys = @($request.text.ToUpperInvariant().ToCharArray() | ForEach-Object { [byte][char]$_ }) }
        else { throw 'Invalid acceptance save name.' }
        foreach ($key in $keys) {
            if ([AcceptanceWindow]::GetForegroundWindow() -ne $window) { throw 'Focus changed during game input.' }
            if (![AcceptanceWindow]::Press($key)) { throw 'Windows did not deliver the test key.' }
        }
    } else { throw 'Unknown game UI action.' }
    return @{}
}

while ($line = [Console]::ReadLine()) {
    try { $result = @{ok = $true; result = (Handle ($line | ConvertFrom-Json))} }
    catch { $result = @{ok = $false; error = $_.Exception.Message} }
    [Console]::WriteLine(($result | ConvertTo-Json -Compress))
}
