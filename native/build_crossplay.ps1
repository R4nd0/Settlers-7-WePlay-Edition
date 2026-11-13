param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$nativeSource = $PSScriptRoot
$nativeOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $nativeOutput | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'The x86 C++ toolchain was not found.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$compilerEnvironment = & $env:ComSpec /d /c "call `"$vcvars`" x64_x86 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the x86 toolchain.' }
foreach ($line in $compilerEnvironment) {
    $position = $line.IndexOf('=')
    if ($position -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0,$position),$line.Substring($position+1),'Process') }
}
Push-Location $nativeOutput
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_channel.cpp') (Join-Path $nativeSource 'crossplay_channel_tests.cpp') /Fe:crossplay_channel_tests.exe /link ws2_32.lib bcrypt.lib "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay channel test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_participant_tests.cpp') /Fe:crossplay_participant_tests.exe /link bcrypt.lib /BASE:0x18000000 /FIXED /DYNAMICBASE:NO "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay participant test build failed.' }
    # The fixed image above is an emulator fixture. Use a relocatable image
    # for Windows execution, where system DLLs can occupy the fixture base.
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_participant_tests.cpp') /Fe:crossplay_participant_runtime_tests.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable native participant test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_admission_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') (Join-Path $nativeSource 'crossplay_channel.cpp') /Fe:crossplay_admission_tests.exe /link ws2_32.lib bcrypt.lib "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay admission test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_chat_tests.cpp') /Fe:crossplay_chat_tests.exe /link "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay chat test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_event_codec_tests.cpp') /Fe:crossplay_event_codec_tests.exe /link bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay event codec test build failed.' }
    # Keep the fixed analysis fixture separate from the relocatable executable
    # run on Windows, where a system DLL may already occupy that image base.
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_event_codec_tests.cpp') /Fe:crossplay_event_codec_runtime_tests.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable native codec test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_preparation_tests.cpp') /Fe:crossplay_preparation_tests.exe /link /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'History preparation test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_session_config_tests.cpp') /Fe:crossplay_session_config_tests.exe /link /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Session configuration test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_ready_tests.cpp') /Fe:crossplay_ready_tests.exe /link /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native Ready request test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_map_reload_tests.cpp') /Fe:crossplay_map_reload_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'History map-reload guard fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_session_map_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_session_map_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'History session-map observer test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_game_start_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_game_start_tests.exe /link ws2_32.lib bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native fresh-start guard fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_game_start_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_game_start_runtime_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable native fresh-start test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_setup_signature_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_setup_signature_tests.exe /link ws2_32.lib bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native setup-signature guard fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_setup_signature_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_setup_signature_runtime_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable setup-signature test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_player_option_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_player_option_tests.exe /link ws2_32.lib bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native player-option guard fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_player_option_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_player_option_runtime_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable native player-option test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_position_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_position_tests.exe /link ws2_32.lib bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native position-list guard fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_position_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_position_runtime_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable native position-list test build failed.' }
    # Fixed analysis fixture for offline CPU execution, not a Windows launch.
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_gold_local_observer_tests.cpp') /Fe:crossplay_gold_local_observer_tests.exe /link ws2_32.lib bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Gold local-event observer ABI fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_gold_start_hold_tests.cpp') /Fe:crossplay_gold_start_hold_tests.exe /link ws2_32.lib bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Gold fresh-start hold ABI fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_closed_slot_tests.cpp') /Fe:crossplay_closed_slot_tests.exe /link ws2_32.lib bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native closed-slot guard fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_closed_slot_tests.cpp') /Fe:crossplay_closed_slot_runtime_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable native closed-slot test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_setup_checkpoint_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_setup_checkpoint_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Diagnostic setup checkpoint test build failed.' }
    # Original player writers use different register/stack ABIs. This fixed
    # fixture is executed only inside the bounded offline CPU emulator.
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_active_player_writer_tests.cpp') /Fe:crossplay_active_player_writer_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Active-player writer ABI fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_preparation_writer_tests.cpp') /Fe:crossplay_preparation_writer_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Whole-preparation writer ABI fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_setup_root_writer_tests.cpp') /Fe:crossplay_setup_root_writer_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Enclosing setup writer ABI fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_setup_root_reader_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_setup_root_reader_tests.exe /link ws2_32.lib bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Original-owned setup reader ABI fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_setup_root_reader_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_setup_root_reader_runtime_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable original-owned setup reader fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_preparation_capture_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_preparation_capture_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Whole-preparation dependency fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_active_setup_capture_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_active_setup_capture_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Active-setup dependency test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_closed_preparation_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_closed_preparation_tests.exe /link ws2_32.lib bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Closed-preparation original setter fixture build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_closed_preparation_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_closed_preparation_runtime_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable closed-preparation test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_setup_root_dependencies_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') /Fe:crossplay_setup_root_dependencies_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Enclosing-rule dependency test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_setup_root_stream_tests.cpp') /Fe:crossplay_setup_root_stream_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Bounded native setup-stream framing test build failed.' }
} finally { Pop-Location }
Write-Output 'Built channel, participant, admission, chat, native codec, History preparation, configuration, Ready intent, map initialization, fresh-start, rule-signature, player-option and position-list tests. No game adapter was installed or launched.'
