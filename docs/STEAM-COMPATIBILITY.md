# Steam History Edition compatibility check — 2026-10-30

**The inspected Steam build is not currently supported by WePlay.** It must
not be accepted by adding its executable hash to the existing native adapter's
allowlist. The supported Ubisoft Connect build is still the working source.

## Inspected original installations

Both originals were inspected read-only. The Steam layout is `Data/Base`, which
the installer's folder resolver already understands. The Ubisoft layout is
`Base`. No Steam account files, caches, credentials or private library state
were accessed. No game binaries are included in this report or the repository.

| Property | Ubisoft Connect | Steam |
| --- | --- | --- |
| EXE SHA256 | `4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f` | `8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410` |
| EXE bytes | 17,333,680 | 17,459,856 |
| PE machine / preferred base | x86 / `0x400000` | x86 / `0x400000` |
| PE timestamp | `0x5d1b15c1` | `0x5d305b24` |
| PE image size | `0x10e7000` | `0x1106000` |
| Entry point RVA | `0x622fb7` | `0x10e3310` |
| Interface.bba SHA256 | `766ae21ef06709309a68fb5bb792b0fd56373c668f02140122eef181917b0b94` | same |

The complete Base-tree comparison found **52 shared paths, 51 with identical
bytes**. The only differing shared file is `Bin/Settlers7R.exe`. Steam also
contains `Bin/steam_api.dll`; Ubisoft contains two additional redistributable
installers under Support. All shared engine DLLs and game archives match.

That asset equality does not establish executable compatibility. The Steam PE
has a different code section, shifted data/readonly-data sections, an additional
`.bind` section and an additional `steam_api.dll` import. The native adapter's
timestamp, image-size and complete-executable hash guards target the Ubisoft
image. Its verified callsites, vtables and absolute addresses cannot be assumed
to carry over to this Steam build.

## Observed result and product behavior

The original source validator accepts the Ubisoft installation and rejects the
Steam installation. The automatic candidate scan includes the standard Steam
folder, but detection correctly excludes its unsupported build. The actual
guarded preparation also stops before creating a separate Steam game copy.
Consequently, no Steam WePlay game, local login, match start, LAN join, Internet
match or sustained simulation has been verified. No multiplayer/desync claim
can be inferred from matching archives.

The identified Steam executable now receives the dedicated
`source_steam_unsupported` setup error instead of a generic suggestion that the
original is modified. The DE/EN message explains that an adapter port is needed
and points to the supported Ubisoft source. Unknown executable hashes remain
rejected; no native, archive or graphics fingerprint guard was relaxed. This
message is in the development sources, not the previously published RC3.5 Setup.

## Required before claiming Steam support

1. Establish a separate Steam executable profile, including its startup path,
   dependencies and the actual in-memory image used for hooks.
2. Map and verify every required native function, callsite, vtable and ABI for
   that profile; retain complete build identity and byte guards.
3. Update preparation, graphics, launch and observation checks consistently;
   make content compatibility between editions an explicit validated contract.
4. Qualify real standalone starts, graphics, Cartographer, local matches and
   both LAN hosting directions, followed by sustained multiplayer tests.

Replacing the Steam EXE with a Ubisoft EXE would test a mixed copy and would not
establish independent Steam-build support. No such substitution was performed.
