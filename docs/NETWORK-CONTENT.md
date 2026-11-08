# Full local integrity and multiplayer compatibility

The renderer correction below is an **unreleased source change** following
RC3.22. It has not been deployed to published downloads or installed launchers.
Native mixed-renderer multiplayer qualification remains required before release.

## Two different checks

`compatibility.json` continues to contain the complete inventory of Base,
Server and Launcher and its original `content_id`. Local `verify()` still checks
every recorded file, including the VC++ installers. Native executable, adapter,
object and ABI guards are unchanged.

For multiplayer, `network_id()` first validates that full manifest, then derives
a separate SHA-256 from the remaining file hashes under the explicit
`s7-play-content-v3` domain. These two exact paths are excluded unconditionally:

- `base/support/vc_redist.x64.exe`
- `base/support/vc_redist.x86.exe`

They are prerequisite installers; WePlay does not run them during a match.
They can differ or be absent between supported source installations even when
the actual game and patch files are identical. Other Support files, game
executables, the native adapter, archives and maps remain part of the network
identity. This does not make different Steam/Ubisoft executable or SDK profiles
equivalent. Service/launcher helpers retain their full local integrity checks.

## Graphics remain local to each PC

RC3.22 includes the selected DXVK renderer in its multiplayer identity. The
guest then tries to adopt the host's renderer before admission. A guest whose
GPU probe cannot confirm DXVK receives `renderer_unsupported`, even after
selecting Automatic or Compatible Graphics locally. Repeating that local
selection cannot satisfy a host that still advertises the Modern profile.

Until both players receive an updated build, close both games, select
**Compatible Graphics**, **save** on both PCs, then let the host restart S7 and
create the game again. Automatic may still select Modern on the host.

The v3 source contract normalizes exactly the complete, pinned
`dxvk-3.1.1-d3d9-60-v1` profile:

| Local path | Required SHA-256 |
| --- | --- |
| `base/bin/d3d9.dll` | `265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa` |
| `base/bin/dxvk.conf` | `26bdbbc2baaad17f2b83f5ac8beb021938404890ea1a90a07011b9fdd7768a48` |

Both hashes and the profile marker must match before either entry is excluded
from the multiplayer identity. Unknown proxies, missing/changed profile files,
unknown profile markers and nearby filenames remain significant. These files
are still present in the full inventory and checked at launch. Full native and
Modern inventory IDs remain different; only their gameplay IDs agree.

Internet pre-admission now compares gameplay identity without changing the
guest's renderer or preferences. LAN preparation applies the local saved policy
before discovery and does not switch to a remote profile. Automatic fallback
to native clears a stale GPU selection and DXVK-only effects, retaining native
detail and quality choices. Explicit unsupported Modern requests and unknown
GPUs on a supported Modern renderer still fail before changing the profile.

The game launchers verify the complete local inventory before LAN admission.
An unexpected local change to an auxiliary installer therefore still stops
startup. GUI listings use validated manifests for their compatibility preview;
that preview does not replace the full file verification at launch.
The explicit full-file comparison CLI retains its nonzero result for different
inventories and explains when the separate network identity nevertheless matches.

## Use the same identity throughout discovery and admission

The GUI's LAN checks, native/Modern profile identities, automatic and manual
LAN launchers, coordinator preflight, directory compatibility, encrypted join
resolution, in-game directory bridge and LAN diagnostic use this network ID.
Service/launcher inventories are still verified before starting a coordinator.
The existing service treats the ID as an opaque, validated 64-character value;
no native patch or transport permission changes are involved.

Legacy full-file and v1/v2 gameplay IDs are not added to the allowed network IDs.
The domain separation prevents accidental acceptance of an old contract. All
players must install the same future build; mixing an RC3.22 launcher with these
development helpers is unsupported. Existing private copies must be rebuilt by
the normal guarded update flow rather than editing a live manifest.

## Offline evidence and remaining qualification

Synthetic fixture checks cover source variants with/without the installers,
changed installer bytes, changed game/adapter/archive/service/helper files,
nearby but non-exempt paths, corrupt manifests, local tampering before discovery,
GUI/launcher/coordinator identity consistency, renderer alternatives, directory
compatibility and rejection of legacy groups. No real game, network endpoint or
running copy is used by these checks. The renderer follow-up passed 255 source
checks, 67 graphics checks and four additional checks including the two isolated
child-Python imports omitted by the guarded runner. The original unsupported
host-renderer error was reproduced before the change.

A separate read-only check of an existing managed copy verified its real
native/Modern profile journal and active profile integrity. Its old v2 IDs
differ; its corrected v3 gameplay IDs agree while full inventory IDs and the
two renderer file differences remain distinct. No game was launched and no
installed game/profile file was changed. This is not a native match or a test
of the remote reporter's PC. See the [verification record](verification/lobby-renderer-20261108.json).

On the Windows development machine, run the bounded source fixture suite with:

```console
python -B tools/testing/run_offline.py
```

It writes results under a new ignored `work/offline-source-tests/` directory,
uses invented files and mocks, and blocks real process, socket, DLL-loading and
registry entry points during the tests. `--focused` limits it to the three
changed behavior suites. Two existing child-Python import checks are omitted
because they require a real process start; omissions are listed in the result.
This runner is for the fixed allowlist of trusted repository tests, not a
security sandbox for arbitrary test code. It does not invoke native or live
integration tests. The sanitized result is recorded in
[the offline verification record](verification/offline-hardening-20261103.json).

Before release, qualify mixed native/Modern modes in isolated native matches,
including admission, public/in-game listing and restart. These checks do not
establish long-match stability or freedom from desync.
