param([Parameter(Mandatory=$true)][string]$Snapshot)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
$initial = Get-Content -LiteralPath $Snapshot -Raw -Encoding UTF8 | ConvertFrom-Json
if ($initial.format -cne 's7-lobby-picker-v1') { exit 2 }
$form = New-Object Windows.Forms.Form
$form.Text = 'Siedler 7 - LAN'; $form.ClientSize = New-Object Drawing.Size(650, 455)
$form.StartPosition = 'CenterScreen'; $form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false; $form.MinimizeBox = $false
$label = New-Object Windows.Forms.Label
$label.Text = 'Dein Spielername'; $label.SetBounds(20, 18, 200, 22); $form.Controls.Add($label)
$name = New-Object Windows.Forms.TextBox
$name.SetBounds(20, 43, 610, 28); $name.Text = $initial.display_name; $name.MaxLength = 24
$form.Controls.Add($name)
$networkLabel = New-Object Windows.Forms.Label
$networkLabel.Text = 'Netzwerk'; $networkLabel.SetBounds(20, 84, 120, 22); $form.Controls.Add($networkLabel)
$network = New-Object Windows.Forms.ComboBox
$network.DropDownStyle = 'DropDownList'; $network.SetBounds(20, 109, 610, 28)
foreach ($item in $initial.networks) { [void]$network.Items.Add([string]$item.name) }
$network.SelectedIndex = 0; $form.Controls.Add($network)
$heading = New-Object Windows.Forms.Label
$heading.Text = 'LAN-Lobbys (werden automatisch gesucht)'; $heading.SetBounds(20, 150, 610, 22)
$form.Controls.Add($heading)
$list = New-Object Windows.Forms.ListBox
$list.SetBounds(20, 178, 610, 155); $list.DisplayMember = 'Label'; $form.Controls.Add($list)
$hint = New-Object Windows.Forms.Label
$hint.SetBounds(20, 342, 610, 45)
$hint.Text = 'Alle Spieler brauchen denselben Patchstand. Im LAN sind Entwickler- und Cheatfunktionen gesperrt.'
$form.Controls.Add($hint)
$create = New-Object Windows.Forms.Button
$create.Text = 'Neue LAN-Lobby'; $create.SetBounds(20, 400, 185, 34); $form.Controls.Add($create)
$join = New-Object Windows.Forms.Button
$join.Text = 'Lobby oeffnen'; $join.SetBounds(220, 400, 185, 34); $form.Controls.Add($join)
$cancel = New-Object Windows.Forms.Button
$cancel.Text = 'Abbrechen'; $cancel.SetBounds(445, 400, 185, 34); $form.Controls.Add($cancel)
$script:selected = $null
$script:revision = -1
$script:rows = @()
function Update-Rooms {
    try {
        $state = Get-Content -LiteralPath $Snapshot -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($state.format -cne 's7-lobby-picker-v1') { return }
        if ($script:revision -eq $state.revision -and $script:networkIndex -eq $network.SelectedIndex) { return }
        $old = if ($list.SelectedItem) { $list.SelectedItem.Key } else { '' }
        $script:revision = $state.revision; $script:networkIndex = $network.SelectedIndex
        $list.BeginUpdate(); $list.Items.Clear()
        foreach ($room in $state.rooms) {
            if ([int]$room.network -ne $network.SelectedIndex) { continue }
            $suffix = if ($room.compatible) { '' } else { ' - anderer Patchstand' }
            $row = [pscustomobject]@{Label=('{0} ({1}/{2}){3}' -f $room.name,$room.members,$room.capacity,$suffix);Key=$room.key;Compatible=$room.compatible}
            [void]$list.Items.Add($row)
            if ($row.Key -ceq $old) { $list.SelectedIndex = $list.Items.Count - 1 }
        }
        $list.EndUpdate()
    } catch { } # An atomic snapshot may be replaced between two reads.
    $join.Enabled = ($null -ne $list.SelectedItem -and $list.SelectedItem.Compatible)
}
function Complete-Choice([string]$Action) {
    $value = $name.Text.Trim()
    if ([string]::IsNullOrWhiteSpace($value) -or [Text.Encoding]::UTF8.GetByteCount($value) -gt 24) {
        [void][Windows.Forms.MessageBox]::Show('Bitte einen Spielernamen mit 1 bis 24 UTF-8-Bytes eingeben.', 'Spielername')
        return
    }
    $key = $null
    if ($Action -ceq 'join') {
        if (-not $list.SelectedItem -or -not $list.SelectedItem.Compatible) { return }
        $key = [string]$list.SelectedItem.Key
    }
    $script:selected = @{format='s7-lobby-choice-v1';action=$Action;network=[int]$network.SelectedIndex;room=$key;display_name=$value}
    $form.Close()
}
$create.Add_Click({ Complete-Choice 'create' })
$join.Add_Click({ Complete-Choice 'join' })
$cancel.Add_Click({ $form.Close() })
$list.Add_SelectedIndexChanged({ $join.Enabled = ($null -ne $list.SelectedItem -and $list.SelectedItem.Compatible) })
$network.Add_SelectedIndexChanged({ Update-Rooms })
$timer = New-Object Windows.Forms.Timer; $timer.Interval = 700
$timer.Add_Tick({ Update-Rooms }); $timer.Start()
try { Update-Rooms; [void]$form.ShowDialog() }
finally { $timer.Stop(); $timer.Dispose(); $form.Dispose() }
if ($null -eq $script:selected) { exit 3 }
$script:selected | ConvertTo-Json -Compress
exit 0
