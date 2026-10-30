# Steam History Edition adapter — 2026-10-30

The development adapter now has a separate, guarded Steam executable profile.
The owner's unchanged Steam EXE reaches the main menu and local login in a
separate copy with DXVK. Two isolated Steam-derived clients have also joined
the same lobby, loaded a map and completed construction commands in a running
multiplayer match. This port is not in the previously released RC3.5 Setup.

## Identified originals

Both original installations remain read-only. Steam uses `Data/Base`; Ubisoft
uses `Base`. No account caches, credentials or private Steam library state are
needed. No game binaries are distributed in this repository.

| Property | Ubisoft Connect | Steam |
| --- | --- | --- |
| EXE SHA256 | `4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f` | `8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410` |
| EXE bytes | 17,333,680 | 17,459,856 |
| PE timestamp | `0x5d1b15c1` | `0x5d305b24` |
| PE image size | `0x10e7000` | `0x1106000` |
| Preferred base / machine | `0x400000` / x86 | `0x400000` / x86 |
| UPLAY_Start product ID | 11788 | 5321 |
| Interface.bba SHA256 | `766ae21ef06709309a68fb5bb792b0fd56373c668f02140122eef181917b0b94` | same |

Of 52 shared Base paths, 51 are byte-identical; the differing shared file is
Settlers7R.exe. Steam additionally needs its original steam_api.dll. All shared
game archives and engine DLLs match. This does not qualify mixed-edition games.

## Native and preparation changes

`native/steam_history.h` maps 137 addresses independently from the Steam image
and supplies 66 build-specific patch guards. PE metadata and the full EXE
digest select the profile. Every affected callsite is checked before any write;
unknown addresses fail closed. ABI, object, thread and rollback guards remain.

Steam actually calls UPLAY_Start with product **5321**, flags zero. Accepting
only Ubisoft's 11788 was a demonstrated startup blocker. Both IDs are now
bound to their corresponding executable and cannot be switched after startup.
Steam store App ID **965320** is a different identifier: local, manual-LAN,
automatic-LAN and Internet launch paths supply it to the child process only.
The original Steam SDK and its ownership checks remain; no API emulator is used.

New packages declare a strict `steam_history` capability and must carry the
matching adapter digest:
`642c5163e74e8fdb2bb3153b3997fa345bf64e851c1f08eb622f9405fca402c5`.
Detection and desktop setup consult this capability. Old packages retain an
explicit upgrade error. Preparation preserves the original EXE digest and
graphics transactions retain their manifest and rollback checks.

The regular prepared copy uses the **unchanged original Steam EXE**. No
unpacker or Ubisoft EXE substitution is needed by production preparation.
Private same-PC laboratory images isolate C/D profile directories and fixed
ports. Only laboratory builds accept their exact hashes. These images and all
analysis products stay under ignored work directories and are not distributed.

## Real observations and limits

- Original Steam EXE: local login, main menu, session browser, running Steam
  client. The owned process actually loaded the private DXVK DLL and Vulkan.
  The existing standard profile showed the game's previous-start recovery
  dialog; tests continued with No, preserving the user's graphics options.
- Two Steam-derived clients: public four-player Tal von Monhaim lobby, join,
  ready state, map load and running simulation. Farm construction completed
  after commands from both host and guest. No spontaneous error appeared
  during this bounded first-match test.
- Switching host roles after leaving a match **without restarting** produced
  session error 0 / link-reach failure. Seamless session reuse is not qualified;
  restart before changing host roles pending a separate lifecycle fix.
- With both clients freshly restarted, the opposite host direction also
  reached the shared lobby and a running Tal von Monhaim match. This does not
  turn the failed in-process role-switch test into a pass.
- Tests used pinned DXVK 3.1.1 x32, a configured 60-FPS limit, Strict float
  emulation, the selected RX 9070 XT and the simple main menu. Both children
  had two known physical cores assigned before their first instruction;
  native topology selection also requested two physical cores. This PC's
  affinity mask is not a portable hardware setting.
- Final single-client private memory peaked at 555 MiB; the first pair peaked at
  1773/1774 MiB. Sentinels reported pressure 0 and no monitor failure. These
  are local memory observations, not measured displayed FPS or a guarantee
  for other hardware.
- Synthetic checks cover mapped patch writes/restoration, corruption rejection
  before writes, normal/lab digest separation, platform startup, CPU selection
  and existing Ubisoft behavior. Python tests use invented fixture files.

Still unqualified: two real Steam PCs in both LAN directions, Steam/Ubisoft
mixed games, Internet matches, extended/endgame simulations, other hardware,
Steam-specific passive observers and the Cartographer. A short local match
does not prove absence of desyncs. Original-build observers must not read
Steam addresses without their own verified profile.
