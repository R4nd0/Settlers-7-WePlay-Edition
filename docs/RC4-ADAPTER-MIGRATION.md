# RC4 adapter migration and package inputs

The common package builder accepts an explicit `--gold-adapter`, alongside its
existing History `--adapter`. Both inputs must match their current production
fingerprint. A previous Gold package may supply verified runtime and resource
inputs, but its old DLL cannot silently become the new output.

Gold build inputs are checked by the current source verifier. Only the exact
known older menu/native fingerprints are accepted as source inputs, and the full
inventory, platform-isolation payload, DXVK and definition guards still apply.
The copied helpers and menu metadata are refreshed. The output contains the
explicit current native DLL and passes the ordinary strict package verifier.
A missing or foreign replacement fails before target staging.

Known old standalone adapters remain recognized when reading existing copies
and deciding whether repair must retain platform isolation. A missing descriptor
does not permit fallback to shared Documents. Fresh preparation still accepts
only the current adapter pins; legacy recognition grants no permission to create
a new copy with an old DLL. Graphics capability checks recognize the exact known
previous History graphics adapter as well as its current replacement.

Seven migration fixtures cover old-descriptor loading, missing-descriptor
protection, fresh-preparation rejection, self-consistent foreign DLL rejection,
graphics recognition, source-only Gold acceptance and current-DLL replacement
with inventory regeneration. An additional private integration fixture rotated
only a copied source tree to an independently rebuilt ordinary AI adapter. It
rebuilt the old Gold template with an explicit replacement, verified the result
using a fresh bundled interpreter, and generated the exact current Gold UI.
The source template remained unchanged. These were package/fixture checks,
not game starts or final crossplay qualification.

The current production fingerprint values are intentionally unchanged by this
migration mechanism. The final qualified crossplay adapters must replace those
values together with the normal launcher/native contract. RC4 publication still
requires all three original editions in the same session, three human clients
plus one AI opponent, saving/loading and stable continuation from the final
installed package. Separate pairs no longer satisfy the updated requirement;
see [the version-3 release gate](RC4-RELOAD-RELEASE-GATE.md).
Private experimental DLLs are not release inputs.

## Ubisoft History resume and desync fixes, 2026-11-18

The final RC4.00 package must include the verified fix for the reported
three-client Ubisoft History desync. Its implementation and regression
evidence are pending. The existing three-client History lab (`9cb60ea`) is
for reproducing and verifying the fix. Build release payloads from ordinary
production sources and keep lab binaries and private saves out of the package.

The current package source includes `18eab7f`. That commit fixes the standard
History resume path for multiplayer Empire mode and preserves the saved co-op
setting. Both reported saves reached the original resume lobby in the verified
run. The existing RC4.00 Candidate1 installer was built from `7af4454` before
that change. The current standalone History DLL pin also predates the fix, so
copying the old adapter into a new build would leave the native NEO correction
out. [Mode 3 verification](receipts/support-resume-mode3-20261117.json)

After the desync fix is verified, rebuild the ordinary standalone History DLL
with the release's directory and NEO settings using `native/build_standalone.ps1`.
Supply it as `--adapter` to `tools/desktop/build_rc2.py` and update the matching
production `HISTORY_PINS` entry for that verified build. The package builder
takes the corrected History resume Lua from the checkout. Preparation composes
it into `Base/BBA/Interface.bba` and checks the result against the selected
archive fingerprint. Verify the native payload and all Lua/archive fingerprints
against the same source revision before final installer qualification. Keep
the Ubisoft History regression from the reported case in addition to the mixed
three-edition reload tests required by the release gate.

For that Ubisoft History regression, load the original autosave from logic turn
27,001 with the final production payload. Compare the three original replay files
from the same reload using `tools/local-pair/replay_crc.py --min-common-checksums 8608`.
Require zero mismatches and verify 20 minutes of continued play from the
shared restart. The replay and live turn evidence must cover both observed
failures, at turns 30,195 and 35,607. The later failure first differs at
zero-based checksum ordinal 8,605. Repeat the 20-minute continuation with the
original manual save and compare its three native replays. Keep the saves and
replays in the private test workspace.
[Regression evidence](SUPPORT-HISTORY-CRASH-20261117.md)

[Migration verification receipt](receipts/rc400-adapter-migration-20261116.json).
