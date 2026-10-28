$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = New-Object Text.UTF8Encoding($false, $true)
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
$request = ConvertFrom-Json ([Console]::In.ReadToEnd())
$cards = @($request.cards)
$prefs = $request.settings
if ($cards.Count -lt 1 -or $cards.Count -gt 32) { throw 'No graphics adapters found.' }
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$form = New-Object Windows.Forms.Form
$form.Text = 'Graphics for The Settlers 7'
$form.ClientSize = New-Object Drawing.Size(700,665)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.Font = New-Object Drawing.Font('Segoe UI',10)
function Add-Label([string]$text,[int]$x,[int]$y,[int]$width=300) {
    $label=New-Object Windows.Forms.Label
    $label.Text=$text
    $label.SetBounds($x,$y,$width,25)
    [void]$form.Controls.Add($label)
}
Add-Label 'Graphics check: game launch is blocked pending investigation' 20 15 660
$gpu=New-Object Windows.Forms.ComboBox
$gpu.DropDownStyle='DropDownList'
$gpu.SetBounds(20,48,660,30)
foreach($card in $cards){[void]$gpu.Items.Add(('{0} ({1} MB)' -f $card.name,$card.vram_mb))}
$gpu.SelectedIndex=0
for($i=0;$i -lt $cards.Count;$i++){if($cards[$i].ordinal -eq $request.adapter){$gpu.SelectedIndex=$i}}
$gpu.Enabled=$false
[void]$form.Controls.Add($gpu)
$combos=@{}
$choices=@(
    @('ShaderQuality','Lighting quality',@('Low','Medium')),
    @('TextureQuality','Texture quality',@('Low','Medium')),
    @('ObjectQuality','Object detail',@('Low','Medium')),
    @('DistanceCulling','View distance',@('Near','Normal')),
    @('ShadowQuality','Shadow detail',@('Low','Medium')),
    @('WaterQuality','Water quality',@('Low','Medium')),
    @('ClutterQuality','Ground detail',@('Low','Medium')),
    @('AA','Anti-aliasing',@('Off','Standard')),
    @('Filtering','Texture filtering',@('Game default','2x anisotropic','4x anisotropic','8x anisotropic','16x anisotropic')),
    @('FPS','Frame rate limit',@('30','60','90','120','144','165','240'))
)
$aaValues=@('off','standard','msaa2','msaa4','msaa8')
$filterValues=@('appdriven','2','4','8','16')
$fpsValues=@(30,60,90,120,144,165,240)
for($i=0;$i -lt $choices.Count;$i++){
    $col=[int]($i%2);$row=[int][Math]::Floor($i/2)
    $x=20+$col*340;$y=100+$row*68
    $key=$choices[$i][0]
    Add-Label $choices[$i][1] $x $y
    $box=New-Object Windows.Forms.ComboBox
    $box.DropDownStyle='DropDownList'
    $box.SetBounds($x,($y+25),320,30)
    foreach($item in $choices[$i][2]){[void]$box.Items.Add($item)}
    $index=0
    if($key -eq 'AA'){$index=[Array]::IndexOf($aaValues,[string]$prefs.AA)}
    elseif($key -eq 'Filtering'){$index=[Array]::IndexOf($filterValues,[string]$prefs.Filtering)}
    elseif($key -eq 'FPS'){$index=[Array]::IndexOf($fpsValues,[int]$prefs.FPS)}
    else{$index=[int]$prefs.$key}
    if($index -lt 0){$index=0}
    if($index -ge $box.Items.Count){$index=0}
    $box.SelectedIndex=$index
    if($key -in @('Filtering','FPS') -or $request.recovery){$box.Enabled=$false}
    $combos[$key]=$box
    [void]$form.Controls.Add($box)
}
Add-Label 'Image effects' 20 450 650
$effects=@{}
$effectLabels=@(@('Shadows','Shadows'),@('SSAO','Ambient occlusion'),
    @('SunShafts','Sun shafts'),@('Parallax','Parallax mapping'),@('DepthOfField','Depth of field'))
for($i=0;$i -lt $effectLabels.Count;$i++){
    $key=$effectLabels[$i][0]
    $check=New-Object Windows.Forms.CheckBox
    $check.Text=$effectLabels[$i][1]
    $check.SetBounds((20+($i%3)*225),(478+[int][Math]::Floor($i/3)*30),220,28)
    $check.Checked=([int]$prefs.$key -eq 1)
    if($key -ne 'Shadows'){$check.Enabled=$false}
    if($request.recovery){$check.Enabled=$false}
    $effects[$key]=$check
    [void]$form.Controls.Add($check)
}
Add-Label 'D3D11: memory errors. D3D9: menu stalls unresolved. FSR unavailable.' 20 548 680
if($request.recovery){Add-Label 'Conservative recovery settings are active after the previous run.' 20 575 680}
else{Add-Label 'GPU selection, filtering and FPS limits cannot be forced in the native fallback.' 20 575 680}
$start=New-Object Windows.Forms.Button
$start.Text='Apply and check'
$start.SetBounds(350,615,205,32)
$start.DialogResult=[Windows.Forms.DialogResult]::OK
$cancel=New-Object Windows.Forms.Button
$cancel.Text='Cancel'
$cancel.SetBounds(565,615,115,32)
$cancel.DialogResult=[Windows.Forms.DialogResult]::Cancel
$form.AcceptButton=$start
$form.CancelButton=$cancel
$form.Controls.AddRange(@($start,$cancel))
try{
    if($form.ShowDialog() -ne [Windows.Forms.DialogResult]::OK){exit 10}
    $selected=@{}
    foreach($key in $combos.Keys){$selected[$key]=[int]$combos[$key].SelectedIndex}
    $selected.AA=$aaValues[$combos.AA.SelectedIndex]
    $selected.Filtering=$filterValues[$combos.Filtering.SelectedIndex]
    $selected.FPS=$fpsValues[$combos.FPS.SelectedIndex]
    foreach($key in $effects.Keys){$selected[$key]=[int]$effects[$key].Checked}
    @{adapter=[int]$cards[$gpu.SelectedIndex].ordinal;settings=$selected}|
        ConvertTo-Json -Compress|ForEach-Object{[Console]::Out.Write($_)}
}finally{$form.Dispose()}
