# Building Settlers-7-WePlay-Edition RC2

The shipped Setup.exe is an offline, per-user Windows x64 application. No original game data or local identity belongs in the build stage. The supported original game is checked on the recipient’s own PC.

## Inputs

- Windows 10 build 19045 or later, .NET SDK 10.0.402, .NET Core and Windows Desktop runtime 10.0.13. The builder copies both runtimes locally; it does not require users to install an SDK.
- Inno Setup 7.1.1 x64, obtained from its official distributor. This project does not redistribute its compiler.
- A verified RC2 installation’s `Package` directory as `--base-package`. It supplies the unchanged, hash-pinned r3 native adapter/service and .NET server runtime. A source rebuild of those components must reproduce their locked hashes before this RC builder accepts it.
- Official CPython 3.14.10 embeddable amd64 ZIP. SHA-256: `90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159`.
- Official DXVK 3.1.1 x32 `d3d9.dll` and its accompanying original LICENSE. DLL SHA-256: `265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa`. The license has its own pin in `graphics_distribution.py`.
- The x86 native graphics probe built by `tools/desktop/build-probe.ps1` using the Windows C++ build tools.

All input paths can be supplied explicitly. Defaults refer to the maintainer’s ignored local work directories and are optional conveniences. Pinned binary inputs and checks remain mandatory.

## Build

From the repository root, publish the WinForms project to an ignored work directory:

```powershell
dotnet restore tools/desktop/S7Launcher.csproj
dotnet publish tools/desktop/S7Launcher.csproj --no-restore -c Release -o work/desktop-app
powershell -File tools/desktop/build-probe.ps1 -OutputDirectory work/desktop-native
python -B tools/desktop/build_rc2.py --stage work/rc2-build --output outputs/WePlay-RC2 --desktop-publish work/desktop-app --dotnet "C:/path/to/dotnet" --probe work/desktop-native/S7GraphicsProbe.exe --compiler "C:/path/to/InnoSetup7/ISCC.exe" --base-package "C:/path/to/installed-WePlay/Package" --python-zip "C:/path/to/python-3.14.10-embed-amd64.zip" --dxvk-dll "C:/path/to/dxvk/x32/d3d9.dll" --dxvk-license "C:/path/to/dxvk/LICENSE"
```

Use a new stage directory for each complete build. The stage must be under `work/`, the delivery under `outputs/`. The installer contains no game copy, State, LocalData or developer PDB files. The builder rejects private state directories and produces `Setup.exe.sha256` beside `Setup.exe`. Runtime notices and licenses accompany the application.

Run `S7Launcher.exe --self-test <output-folder>` from the completed stage, where its `DesktopRuntime` exists. The bare publish folder intentionally needs that runtime before the executable can run. The self-test checks bilingual layouts, translation encoding and display/GPU controls; it does not start or verify a game.

These are maintainer instructions. Players only run the released Setup.exe and use the one WePlay starter.
