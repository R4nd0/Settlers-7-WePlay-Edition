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
    param($Choice)
    if ($null -ne $Choice -and -not (Test-ModelName $Choice)) { throw 'Invalid model choice.' }
    return (@{format = 's7-graphics-choice-v1'; choice = $Choice} | ConvertTo-Json -Compress)
}

try {
    [Console]::InputEncoding = New-Object Text.UTF8Encoding($false, $true)
    [Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
    $inputText = [Console]::In.ReadToEnd()
    if ([Text.Encoding]::UTF8.GetByteCount($inputText) -gt 4096) { throw 'Input too large.' }
    $previous = $inputText | ConvertFrom-Json
    if ($previous.format -cne 's7-graphics-choice-v1') { throw 'Unknown choice format.' }
    if ($null -ne $previous.choice -and -not (Test-ModelName $previous.choice)) { throw 'Invalid previous choice.' }

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
    $form.Text = 'Grafikkarte fuer Siedler 7'
    $form.ClientSize = New-Object Drawing.Size(590, 370)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.AutoScaleMode = 'Font'
    $form.Font = New-Object Drawing.Font('Segoe UI', 10)

    $description = New-Object Windows.Forms.Label
    $description.SetBounds(18, 16, 550, 64)
    if ($filterAvailable) {
        $description.Text = 'Waehle die Grafikkarte fuer Siedler 7. Deine Auswahl wird auf diesem PC gespeichert und beim naechsten Start wieder angezeigt.'
    } else {
        $description.Text = 'Standardgrafik: Windows und das Spiel uebernehmen die Grafikkartenwahl. Fuer die direkte Auswahl muss ein passendes Grafikprofil aktiviert sein.'
    }
    [void]$form.Controls.Add($description)

    $combo = New-Object Windows.Forms.ComboBox
    $combo.SetBounds(18, 88, 550, 28)
    $combo.DropDownStyle = 'DropDownList'
    [void]$combo.Items.Add('Automatisch')
    foreach ($model in $models) { [void]$combo.Items.Add($model) }
    $combo.SelectedIndex = 0
    if ($null -ne $preferred) { $combo.SelectedItem = $preferred }
    $combo.Enabled = $filterAvailable
    [void]$form.Controls.Add($combo)

    $detected = New-Object Windows.Forms.Label
    $detected.SetBounds(18, 130, 550, 24)
    $detected.Text = 'Erkannte Modelle (gleiche Namen erscheinen einmal):'
    [void]$form.Controls.Add($detected)
    $names = New-Object Windows.Forms.TextBox
    $names.SetBounds(18, 156, 550, 82)
    $names.Multiline = $true
    $names.ReadOnly = $true
    $names.ScrollBars = 'Vertical'
    $names.TabStop = $false
    $names.Text = if ($models.Count) { $models -join [Environment]::NewLine } else { 'Keine Modellnamen verfuegbar.' }
    [void]$form.Controls.Add($names)

    $hint = New-Object Windows.Forms.Label
    $hint.SetBounds(18, 250, 550, 58)
    if ($enumerationFailed) {
        $hint.Text = 'Die Modelle konnten nicht ausgelesen werden. Automatisch bleibt verfuegbar.'
    } elseif ($filterAvailable -and $null -ne $previous.choice -and $null -eq $preferred) {
        $hint.Text = 'Die zuletzt gewaehlte Karte wurde nicht gefunden. Automatisch ist ausgewaehlt. Gleichnamige Karten lassen sich hier nicht einzeln unterscheiden.'
    } else {
        $hint.Text = 'Wenn mehrere Karten denselben Modellnamen haben, gilt die Auswahl fuer diese Modellgruppe.'
    }
    [void]$form.Controls.Add($hint)

    $start = New-Object Windows.Forms.Button
    $start.SetBounds(352, 324, 104, 30)
    $start.Text = 'Start'
    $start.DialogResult = [Windows.Forms.DialogResult]::OK
    $cancel = New-Object Windows.Forms.Button
    $cancel.SetBounds(464, 324, 104, 30)
    $cancel.Text = 'Abbrechen'
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
    $form.Dispose()
    if ($answer -ne [Windows.Forms.DialogResult]::OK) { exit 10 }
    [Console]::Out.WriteLine((ConvertTo-ChoiceJson $choice))
    exit 0
} catch {
    [Console]::Error.WriteLine('Der Grafikauswahldialog konnte nicht angezeigt werden.')
    exit 2
}
