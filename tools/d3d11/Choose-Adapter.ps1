$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$cards = @(ConvertFrom-Json ([Console]::In.ReadToEnd()))
if ($cards.Count -lt 1 -or $cards.Count -gt 32) { throw 'Keine Grafikkarten gefunden' }
$form = New-Object System.Windows.Forms.Form
$form.Text = 'S7 – D3D11-Grafikkarte'
$form.ClientSize = New-Object System.Drawing.Size(530,155)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$label = New-Object System.Windows.Forms.Label
$label.Text = 'Grafikkarte für diesen Spielstart wählen:'
$label.SetBounds(18,18,490,25)
$combo = New-Object System.Windows.Forms.ComboBox
$combo.DropDownStyle = 'DropDownList'
$combo.SetBounds(18,50,490,30)
foreach ($card in $cards) {
    $null = $combo.Items.Add(('{0} ({1} MB Grafikspeicher)' -f $card.name,$card.vram_mb))
}
$combo.SelectedIndex = 0
$start = New-Object System.Windows.Forms.Button
$start.Text = 'Spiel starten'
$start.SetBounds(275,103,115,30)
$start.DialogResult = [System.Windows.Forms.DialogResult]::OK
$cancel = New-Object System.Windows.Forms.Button
$cancel.Text = 'Abbrechen'
$cancel.SetBounds(400,103,108,30)
$cancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
$form.AcceptButton = $start
$form.CancelButton = $cancel
$form.Controls.AddRange(@($label,$combo,$start,$cancel))
try {
    if ($form.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 10 }
    [Console]::Out.Write([string]$cards[$combo.SelectedIndex].ordinal)
} finally { $form.Dispose() }
