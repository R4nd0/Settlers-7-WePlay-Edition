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
    param($Choice, $SimpleMenu = $false)
    if ($null -ne $Choice -and -not (Test-ModelName $Choice)) { throw 'Invalid model choice.' }
    if ($SimpleMenu -isnot [bool]) { throw 'Invalid simple menu choice.' }
    return (@{format = 's7-graphics-choice-v1'; choice = $Choice; simple_menu = $SimpleMenu} | ConvertTo-Json -Compress)
}

try {
    [Console]::InputEncoding = New-Object Text.UTF8Encoding($false, $true)
    [Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
    $inputText = [Console]::In.ReadToEnd()
    if ([Text.Encoding]::UTF8.GetByteCount($inputText) -gt 4096) { throw 'Input too large.' }
    $previous = $inputText | ConvertFrom-Json
    if ($previous.format -cne 's7-graphics-choice-v1') { throw 'Unknown choice format.' }
    if ($null -ne $previous.choice -and -not (Test-ModelName $previous.choice)) { throw 'Invalid previous choice.' }
    $fields = @($previous.PSObject.Properties.Name)
    if ($fields.Count -lt 2 -or $fields.Count -gt 3 -or
        @($fields | Where-Object { $_ -cnotin @('format', 'choice', 'simple_menu') }).Count -gt 0 -or
        'choice' -cnotin $fields) { throw 'Unknown choice fields.' }
    $simpleMenuEnabled = $false
    if ('simple_menu' -cin $fields) {
        if ($previous.simple_menu -isnot [bool]) { throw 'Invalid previous simple menu choice.' }
        $simpleMenuEnabled = $previous.simple_menu
    }

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
    $form.ClientSize = New-Object Drawing.Size(590, 490)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.AutoScaleMode = 'Font'
    $form.Font = New-Object Drawing.Font('Segoe UI', 10)

    $description = New-Object Windows.Forms.Label
    $description.SetBounds(18, 16, 550, 64)
    if ($filterAvailable) {
        $description.Text = 'Choose a graphics card for The Settlers 7. Your choice is saved on this PC and shown again at the next launch.'
    } else {
        $description.Text = 'Standard graphics: Windows and the game select the GPU. Direct GPU selection requires a compatible graphics profile.'
    }
    [void]$form.Controls.Add($description)

    $combo = New-Object Windows.Forms.ComboBox
    $combo.SetBounds(18, 88, 550, 28)
    $combo.DropDownStyle = 'DropDownList'
    [void]$combo.Items.Add('Automatic')
    foreach ($model in $models) { [void]$combo.Items.Add($model) }
    $combo.SelectedIndex = 0
    if ($null -ne $preferred) { $combo.SelectedItem = $preferred }
    $combo.Enabled = $filterAvailable
    [void]$form.Controls.Add($combo)

    $detected = New-Object Windows.Forms.Label
    $detected.SetBounds(18, 130, 550, 24)
    $detected.Text = 'Detected models (duplicate names are grouped):'
    [void]$form.Controls.Add($detected)
    $names = New-Object Windows.Forms.TextBox
    $names.SetBounds(18, 156, 550, 82)
    $names.Multiline = $true
    $names.ReadOnly = $true
    $names.ScrollBars = 'Vertical'
    $names.TabStop = $false
    $names.Text = if ($models.Count) { $models -join [Environment]::NewLine } else { 'No model names available.' }
    [void]$form.Controls.Add($names)

    $hint = New-Object Windows.Forms.Label
    $hint.SetBounds(18, 250, 550, 58)
    if ($enumerationFailed) {
        $hint.Text = 'The graphics adapters could not be listed. Automatic selection is still available.'
    } elseif ($filterAvailable -and $null -ne $previous.choice -and $null -eq $preferred) {
        $hint.Text = 'The previous GPU was not found. Automatic is selected. GPUs with identical names cannot be selected individually.'
    } else {
        $hint.Text = 'If multiple GPUs share the same model name, your choice applies to that model group.'
    }
    [void]$form.Controls.Add($hint)

    $simpleMenu = New-Object Windows.Forms.CheckBox
    $simpleMenu.SetBounds(18, 314, 550, 30)
    $simpleMenu.Text = 'Simple main menu + VSync (lower CPU usage, experimental)'
    $simpleMenu.Checked = $simpleMenuEnabled
    [void]$form.Controls.Add($simpleMenu)
    $menuHint = New-Object Windows.Forms.Label
    $menuHint.SetBounds(18, 350, 550, 86)
    $menuHint.Text = 'Disables the menu landscape and some castle/content previews. VSync follows the display refresh rate. Experimental: longer matches and LAN with this option are not yet verified.'
    [void]$form.Controls.Add($menuHint)

    $start = New-Object Windows.Forms.Button
    $start.SetBounds(352, 444, 104, 30)
    $start.Text = 'Start'
    $start.DialogResult = [Windows.Forms.DialogResult]::OK
    $cancel = New-Object Windows.Forms.Button
    $cancel.SetBounds(464, 444, 104, 30)
    $cancel.Text = 'Cancel'
    $cancel.DialogResult = [Windows.Forms.DialogResult]::Cancel
    [void]$form.Controls.Add($start)
    [void]$form.Controls.Add($cancel)
    $form.AcceptButton = $start
    $form.CancelButton = $cancel
    $answer = $form.ShowDialog()
    $choice = $null
    if ($answer -eq [Windows.Forms.DialogResult]::OK -and $filterAvailable -and $combo.SelectedIndex -gt 0) {
        $choice = [string]$combo.SelectedItem
    }
    $selectedSimpleMenu = [bool]$simpleMenu.Checked
    $form.Dispose()
    if ($answer -ne [Windows.Forms.DialogResult]::OK) { exit 10 }
    [Console]::Out.WriteLine((ConvertTo-ChoiceJson $choice $selectedSimpleMenu))
    exit 0
} catch {
    [Console]::Error.WriteLine('The graphics dialog could not be displayed.')
    exit 2
}
