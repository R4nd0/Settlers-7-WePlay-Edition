# Full local integrity and multiplayer compatibility

This is an **unreleased source change**. Published RC3.13 binaries, private ZIPs
and running copies are unchanged. Live two-client verification is still required
before releasing it.

## Two different checks

`compatibility.json` continues to contain the complete inventory of Base,
Server and Launcher and its original `content_id`. Local `verify()` still checks
every recorded file, including the VC++ installers. Native executable, adapter,
object and ABI guards are unchanged.

For multiplayer, `network_id()` first validates that full manifest, then derives
a separate SHA-256 from the remaining file hashes under the explicit
`s7-play-content-v1` domain. Only these two exact paths are excluded:

- `base/support/vc_redist.x64.exe`
- `base/support/vc_redist.x86.exe`

They are prerequisite installers; WePlay does not run them during a match.
They can differ or be absent between supported source installations even when
the actual game and patch files are identical. No other Support file, executable,
DLL, archive, map, service or launcher helper is excluded. This does not make
different Steam/Ubisoft executable or SDK profiles equivalent.

The game launchers verify the complete local inventory before LAN admission.
An unexpected local change to an auxiliary installer therefore still stops
startup. GUI listings use validated manifests for their compatibility preview;
that preview does not replace the full file verification at launch.
The explicit full-file comparison CLI retains its nonzero result for different
inventories and explains when the separate network identity nevertheless matches.

## Use the same identity throughout discovery and admission

The GUI's LAN checks, native/Modern renderer alternatives, automatic and manual
LAN launchers, coordinator preflight, directory compatibility, encrypted join
resolution, in-game directory bridge and LAN diagnostic use this network ID.
Service/launcher inventories are still verified before starting a coordinator.
The existing service treats the ID as an opaque, validated 64-character value;
no native patch or transport permission changes are involved.

Legacy full-file IDs are deliberately not added to the allowed network IDs.
The domain separation prevents accidental acceptance of an old contract. All
players must install the same future build; mixing an RC3.13 binary with these
development helpers is unsupported. Existing private copies must be rebuilt by
the normal guarded update flow rather than editing a live manifest.

## Offline evidence and remaining qualification

Synthetic fixture checks cover source variants with/without the installers,
changed installer bytes, changed game/adapter/archive/service/helper files,
nearby but non-exempt paths, corrupt manifests, local tampering before discovery,
GUI/launcher/coordinator identity consistency, renderer alternatives, directory
compatibility and rejection of legacy groups. No real game, network endpoint or
running copy is used by these checks.

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

After the user explicitly permits live work again, qualify the helpers with two isolated real
clients, including a source with the VC++ installers and one without. Check
native and Modern mode alignment, admission, public/in-game listing and restart.
This source work does not establish long-match stability or freedom from desync.
