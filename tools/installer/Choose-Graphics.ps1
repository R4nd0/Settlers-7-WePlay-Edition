param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('dxvk', 'native')]
    [string]$RendererMode
)

$ErrorActionPreference = 'Stop'

function Test-ModelName {
    param($Value)
    if ($Value -isnot [string] -or [string]::IsNullOrWhiteSpace($Value)) { return $false }
    if ($Value -cne $Value.Trim()) { return $false }
    for ($i = 0; $i -lt $Value.Length; $i++) {
        $category = [Globalization.CharUnicodeInfo]::GetUnicodeCategory($Value, $i)
        if ($category -in @([Globalization.UnicodeCategory]::Control,
                           [Globalization.UnicodeCategory]::Format,
                           [Globalization.UnicodeCategory]::Surrogate,
                           [Globalization.UnicodeCategory]::PrivateUse,
                           [Globalization.UnicodeCategory]::OtherNotAssigned)) { return $false }
        if ([char]::IsHighSurrogate($Value[$i])) { $i++ }
    }
    return ([Text.Encoding]::UTF8.GetByteCount($Value) -le 256)
}

function Normalize-ModelNames {
    param([object[]]$Names)
    $seen = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($name in ($Names | Select-Object -First 64)) {
        if ($name -is [string]) {
            $clean = $name.Trim()
            if (Test-ModelName $clean) { [void]$seen.Add($clean) }
        }
    }
    return @($seen | Sort-Object)
}

function Resolve-PreferredChoice {
    param($Preferred, [string[]]$Models, [bool]$FilterAvailable)
    if (-not $FilterAvailable -or -not (Test-ModelName $Preferred)) { return $null }
    foreach ($model in $Models) {
        if ([string]::Equals($model, $Preferred, [StringComparison]::OrdinalIgnoreCase)) { return $model }
    }
    return $null
}

function ConvertTo-ChoiceJson {
    param($Choice, $SimpleMenu = $false, $Display = $null)
    if ($null -ne $Choice -and -not (Test-ModelName $Choice)) { throw 'Invalid model choice.' }
    if ($SimpleMenu -isnot [bool]) { throw 'Invalid simple menu choice.' }
    if (-not (Test-DisplayChoice $Display)) { throw 'Invalid display choice.' }
    $record = @{format = 's7-graphics-choice-v1'; choice = $Choice; simple_menu = $SimpleMenu}
    if ($PSBoundParameters.ContainsKey('Display')) { $record.display = $Display }
    return ($record | ConvertTo-Json -Compress -Depth 5)
}

function Test-DisplayChoice {
    param($Value)
    if ($null -eq $Value) { return $true }
    if ($Value -is [Collections.IDictionary]) { $keys = @($Value.Keys) }
    else { $keys = @($Value.PSObject.Properties.Name) }
    if ($keys.Count -ne 4 -or @($keys | Where-Object { $_ -cnotin @('device','width','height','mode') }).Count) { return $false }
    if ($Value.mode -isnot [string]) { return $false }
    if ($Value.device -isnot [string] -or $Value.device -cnotmatch '^\\\\\.\\DISPLAY[1-9][0-9]{0,2}$') { return $false }
    foreach ($key in @('width','height')) {
        if ($Value.$key -isnot [int] -and $Value.$key -isnot [long]) { return $false }
    }
    return ($Value.width -ge 800 -and $Value.width -le 3840 -and $Value.height -ge 600 -and
        $Value.height -le 3840 -and ([long]$Value.width * $Value.height) -le (3840 * 2160) -and
        $Value.mode -cin @('windowed','borderless'))
}

try {
    [Console]::InputEncoding = New-Object Text.UTF8Encoding($false, $true)
    [Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
    $inputText = [Console]::In.ReadToEnd()
    if ([Text.Encoding]::UTF8.GetByteCount($inputText) -gt 65536) { throw 'Input too large.' }
    $request = $inputText | ConvertFrom-Json
    if (@($request.PSObject.Properties.Name).Count -ne 2 -or
        'previous' -cnotin @($request.PSObject.Properties.Name) -or
        'displays' -cnotin @($request.PSObject.Properties.Name)) { throw 'Unknown dialog request.' }
    $previous = $request.previous
    if (@($request.displays).Count -gt 32) { throw 'Too many displays.' }
    $displayEntries = @($request.displays | Select-Object -First 2)
    if ($previous.format -cne 's7-graphics-choice-v1') { throw 'Unknown choice format.' }
    if ($null -ne $previous.choice -and -not (Test-ModelName $previous.choice)) { throw 'Invalid previous choice.' }
    $fields = @($previous.PSObject.Properties.Name)
    if ($fields.Count -lt 2 -or $fields.Count -gt 4 -or
        @($fields | Where-Object { $_ -cnotin @('format', 'choice', 'simple_menu','display') }).Count -gt 0 -or
        'choice' -cnotin $fields) { throw 'Unknown choice fields.' }
    $simpleMenuEnabled = $false
    if ('simple_menu' -cin $fields) {
        if ($previous.simple_menu -isnot [bool]) { throw 'Invalid previous simple menu choice.' }
        $simpleMenuEnabled = $previous.simple_menu
    }
    if (-not (Test-DisplayChoice $previous.display)) { throw 'Invalid previous display.' }

    $enumerationFailed = $false
    try {
        # Deliberately request only model names. No device IDs, serials,
        # registry values, accounts, driver identifiers or Vulkan claims.
        $rawNames = @(Get-CimInstance -ClassName Win32_VideoController -Property Name -ErrorAction Stop |
            ForEach-Object { $_.Name })
        $models = @(Normalize-ModelNames $rawNames)
    } catch {
        $models = @()
        $enumerationFailed = $true
    }
    $filterAvailable = $RendererMode -eq 'dxvk'
    $preferred = Resolve-PreferredChoice $previous.choice $models $filterAvailable

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [Windows.Forms.Application]::EnableVisualStyles()
    $form = New-Object Windows.Forms.Form
    $form.Text = 'Graphics for The Settlers 7'
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.AutoScaleMode = 'Font'
    $form.Font = New-Object Drawing.Font('Segoe UI', 10)
    $form.ClientSize = New-Object Drawing.Size(650, 640)

    $description = New-Object Windows.Forms.Label
    $description.SetBounds(18, 16, 614, 48)
    if ($filterAvailable) {
        $description.Text = 'Choose a graphics card for The Settlers 7. Your choice is saved on this PC and shown again at the next launch.'
    } else {
        $description.Text = 'Standard graphics: Windows and the game select the GPU. Direct GPU selection requires a compatible graphics profile.'
    }
    [void]$form.Controls.Add($description)

    $combo = New-Object Windows.Forms.ComboBox
    $gpuLabel = New-Object Windows.Forms.Label
    $gpuLabel.Text = 'Graphics card'
    $gpuLabel.SetBounds(18, 68, 614, 22)
    [void]$form.Controls.Add($gpuLabel)
    $combo.SetBounds(18, 94, 614, 28)
    $combo.DropDownStyle = 'DropDownList'
    [void]$combo.Items.Add('Automatic')
    foreach ($model in $models) { [void]$combo.Items.Add($model) }
    $combo.SelectedIndex = 0
    if ($null -ne $preferred) { $combo.SelectedItem = $preferred }
    $combo.Enabled = $filterAvailable
    [void]$form.Controls.Add($combo)

    $hint = New-Object Windows.Forms.Label
    $hint.SetBounds(18, 132, 614, 42)
    if ($enumerationFailed) {
        $hint.Text = 'The graphics adapters could not be listed. Automatic selection is still available.'
    } elseif ($filterAvailable -and $null -ne $previous.choice -and $null -eq $preferred) {
        $hint.Text = 'The previous GPU was not found. Automatic is selected. GPUs with identical names cannot be selected individually.'
    } else {
        $hint.Text = 'If multiple GPUs share the same model name, your choice applies to that model group.'
    }
    [void]$form.Controls.Add($hint)

    $keepDisplay = New-Object Windows.Forms.CheckBox
    $keepDisplay.Text = 'Use current game display settings'
    $keepDisplay.SetBounds(18, 180, 614, 28)
    $keepDisplay.Checked = $null -eq $previous.display
    [void]$form.Controls.Add($keepDisplay)
    foreach ($item in @(@('Target display',18,218,614), @('Window mode',18,284,296), @('Resolution',336,284,296))) {
        $label = New-Object Windows.Forms.Label
        $label.Text = $item[0]
        $label.SetBounds($item[1],$item[2],$item[3],22)
        [void]$form.Controls.Add($label)
    }
    $displayCombo = New-Object Windows.Forms.ComboBox
    $displayCombo.DropDownStyle = 'DropDownList'
    $displayCombo.SetBounds(18,244,614,28)
    foreach ($entry in $displayEntries) {
        $suffix = if ($entry.primary) { ' (primary)' } else { '' }
        $displayNumber = $entry.device -replace '^.*DISPLAY',''
        [void]$displayCombo.Items.Add(('Display {0}{1} - {2} x {3}' -f $displayNumber,$suffix,$entry.width,$entry.height))
    }
    if ($displayEntries.Count) {
        $displayCombo.SelectedIndex = 0
        for ($i=0; $i -lt $displayEntries.Count; $i++) {
            if ($displayEntries[$i].device -ceq $previous.display.device) { $displayCombo.SelectedIndex = $i }
        }
    } else {
        [void]$displayCombo.Items.Add('No display information available')
        $displayCombo.SelectedIndex = 0
        $keepDisplay.Checked = $true
        $keepDisplay.Enabled = $false
    }
    [void]$form.Controls.Add($displayCombo)
    $modeCombo = New-Object Windows.Forms.ComboBox
    $modeCombo.DropDownStyle = 'DropDownList'
    $modeCombo.SetBounds(18,310,296,28)
    [void]$modeCombo.Items.Add('Windowed')
    [void]$modeCombo.Items.Add('Fullscreen (borderless)')
    $modeCombo.SelectedIndex = if ($previous.display.mode -ceq 'borderless') { 1 } else { 0 }
    [void]$form.Controls.Add($modeCombo)
    $resolutionCombo = New-Object Windows.Forms.ComboBox
    $resolutionCombo.DropDownStyle = 'DropDownList'
    $resolutionCombo.SetBounds(336,310,296,28)
    [void]$form.Controls.Add($resolutionCombo)
    $displayHint = New-Object Windows.Forms.Label
    $displayHint.SetBounds(18,350,614,54)
    [void]$form.Controls.Add($displayHint)

    function Update-DisplayState {
        $custom = -not $keepDisplay.Checked
        foreach ($control in @($displayCombo,$modeCombo,$resolutionCombo)) { $control.Enabled = $custom }
        if ($modeCombo.SelectedIndex -eq 1) { $resolutionCombo.Enabled = $false }
        $valid = $displayEntries.Count -gt 0 -and $resolutionCombo.SelectedIndex -ge 0
        if ($custom -and $filterAvailable -and $combo.SelectedIndex -gt 0 -and $valid -and
            -not [string]::Equals([string]$combo.SelectedItem,[string]$displayEntries[$displayCombo.SelectedIndex].adapter,[StringComparison]::OrdinalIgnoreCase)) {
            $valid = $false
            $displayHint.Text = 'Choose Automatic or the graphics card connected to this display.'
        } elseif (-not $displayEntries.Count) {
            $displayHint.Text = 'Display information is unavailable. Current game settings will be kept.'
        } elseif ($modeCombo.SelectedIndex -eq 1) {
            $displayHint.Text = 'Borderless fullscreen uses the selected desktop resolution without changing your monitor arrangement.'
        } else {
            $displayHint.Text = 'The first two active displays are supported. Resolution choices are limited to keep memory use manageable.'
        }
        $start.Enabled = $keepDisplay.Checked -or $valid
    }
    function Update-ResolutionChoices {
        $resolutionCombo.Items.Clear()
        if ($displayEntries.Count) {
            $entry = $displayEntries[$displayCombo.SelectedIndex]
            $preferredSize = @([int]$entry.width,[int]$entry.height)
            if ($entry.device -ceq $previous.display.device -and $modeCombo.SelectedIndex -ne 1) { $preferredSize = @([int]$previous.display.width,[int]$previous.display.height) }
            $preferredIndex = 0
            $sizes = @($entry.resolutions)
            if ($modeCombo.SelectedIndex -eq 1) {
                $sizes = @($entry.resolutions | Where-Object { $_[0] -eq $entry.width -and $_[1] -eq $entry.height })
            }
            for ($i=0; $i -lt $sizes.Count; $i++) {
                $size = $sizes[$i]
                [void]$resolutionCombo.Items.Add(('{0} x {1}' -f $size[0],$size[1]))
                if ($size[0] -eq $preferredSize[0] -and $size[1] -eq $preferredSize[1]) { $preferredIndex = $i }
            }
            if ($resolutionCombo.Items.Count) { $resolutionCombo.SelectedIndex = $preferredIndex }
        }
        Update-DisplayState
    }

    $simpleMenu = New-Object Windows.Forms.CheckBox
    $simpleMenu.SetBounds(18, 414, 614, 30)
    $simpleMenu.Text = 'Simple main menu + VSync (lower CPU usage, experimental)'
    $simpleMenu.Checked = $simpleMenuEnabled
    [void]$form.Controls.Add($simpleMenu)
    $menuHint = New-Object Windows.Forms.Label
    $menuHint.SetBounds(18, 452, 614, 100)
    $menuHint.Text = 'Disables the menu landscape and some castle/content previews. VSync follows the display refresh rate. Experimental: longer matches and LAN with this option are not yet verified.'
    [void]$form.Controls.Add($menuHint)

    $start = New-Object Windows.Forms.Button
    $start.SetBounds(414, 590, 104, 30)
    $start.Text = 'Start'
    $start.DialogResult = [Windows.Forms.DialogResult]::OK
    $cancel = New-Object Windows.Forms.Button
    $cancel.SetBounds(526, 590, 104, 30)
    $cancel.Text = 'Cancel'
    $cancel.DialogResult = [Windows.Forms.DialogResult]::Cancel
    [void]$form.Controls.Add($start)
    [void]$form.Controls.Add($cancel)
    $form.AcceptButton = $start
    $form.CancelButton = $cancel
    $displayCombo.add_SelectedIndexChanged({ Update-ResolutionChoices })
    $modeCombo.add_SelectedIndexChanged({ Update-ResolutionChoices })
    $combo.add_SelectedIndexChanged({ Update-DisplayState })
    $keepDisplay.add_CheckedChanged({ Update-DisplayState })
    $resolutionCombo.add_SelectedIndexChanged({ Update-DisplayState })
    Update-ResolutionChoices
    $answer = $form.ShowDialog()
    if ($answer -ne [Windows.Forms.DialogResult]::OK) { $form.Dispose(); exit 10 }
    $choice = $null
    if ($answer -eq [Windows.Forms.DialogResult]::OK -and $filterAvailable -and $combo.SelectedIndex -gt 0) {
        $choice = [string]$combo.SelectedItem
    }
    $selectedSimpleMenu = [bool]$simpleMenu.Checked
    $selectedDisplay = $null
    if (-not $keepDisplay.Checked) {
        $entry = $displayEntries[$displayCombo.SelectedIndex]
        $size = $entry.resolutions[$resolutionCombo.SelectedIndex]
        if ($modeCombo.SelectedIndex -eq 1) { $size = @([int]$entry.width,[int]$entry.height) }
        $selectedDisplay = @{device=[string]$entry.device; width=[int]$size[0]; height=[int]$size[1];
            mode=$(if ($modeCombo.SelectedIndex -eq 1) {'borderless'} else {'windowed'})}
    }
    $form.Dispose()
    [Console]::Out.WriteLine((ConvertTo-ChoiceJson $choice $selectedSimpleMenu $selectedDisplay))
    exit 0
} catch {
    [Console]::Error.WriteLine('The graphics dialog could not be displayed.')
    exit 2
}
