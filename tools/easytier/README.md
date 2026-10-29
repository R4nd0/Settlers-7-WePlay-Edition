# WePlay EasyTier runtime

The selected runtime is EasyTier v2.6.4, upstream revision `8428a89`, built as
`2.6.4-weplay-npcap-free` for Windows 10/11 x64. WePlay's game and native adapter
remain 32-bit; the VPN runs in a separate process. The authoritative distributable
file inventory is `tools/installer/easytier-runtime.json`.

## Source build

Use the official source archive and protoc 26.0 Windows x64 ZIP:

- [EasyTier source](https://api.github.com/repos/EasyTier/EasyTier/zipball/v2.6.4), SHA256 `57eb069465ad52bc328bec597c820163afc94d85469e9299f2b8b210c71897b4`.
- [protoc](https://github.com/protocolbuffers/protobuf/releases/download/v26.0/protoc-26.0-win64.zip), SHA256 `f5a0e21c09e823fe5cc18d6057f8db7a3d5df420c09314d548b9139c4193156d`.

With Rust stable 1.99.0 and the MSVC x64 C++ tools, run `build-runtime.ps1` with
`-SourceZip`, `-ProtocZip` and a new `-OutputDirectory` under repository `work/`.
The build uses `cargo --locked -j 1 --no-default-features --features
tun,aes-gcm,smoltcp`. It never starts a tunnel or installs a service.

The reviewed patch removes the Windows pnet/Npcap enumeration fallback and fails
closed if the Windows adapter API fails. Disabling `faketcp` removes the other
capture path. It also omits Windows 7 thunk-library downloads and gives the build
an explicit modified version instead of accidentally identifying the enclosing
WePlay Git checkout. Preserve the original upstream copyright and licenses.

The stock Windows core has a hard dependency on its supplied Npcap `Packet.dll`.
The modified core and CLI were started and connected without that DLL. Do not
ship it or the unused `WinDivert64.sys` driver in this runtime.

## Runtime assembly and source availability

The runtime contains the two built executables, unmodified Wintun 0.14.1 x64,
LGPL 3.0 and GPL 3.0 texts, the Wintun binary license, third-party notices and
`SOURCE.txt`. Wintun's DLL must exactly match the official
[0.14.1 ZIP](https://www.wintun.net/builds/wintun-0.14.1.zip), SHA256
`07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51`.

Collect dependency notices from Cargo's resolved packages and their exact source
revisions, including license directories, source-header notices and subcrate
licenses held in the upstream repository root. Retain all copyright notices.
The reviewed notice collection conservatively includes build dependencies too.
Rebuilding can change executable bytes; updating a pin is a reviewed source
change, not an automatic repair of a mismatched download.

Ship the corresponding-source ZIP produced by `source_bundle.py` alongside any
distributed preview. It retains the complete source tree and Cargo.lock, applies
the reviewed patch, and includes that patch and build instructions. Upstream's
prebuilt native binaries are omitted, including its non-redistributable Npcap
DLL. Runtime dependency notices accompany the executables. Source and
binary intermediates belong in ignored `work/`; user-facing bundles belong in
ignored `outputs/`. Never include game files, active room configurations, local
identities, invitations or process state in a package.
