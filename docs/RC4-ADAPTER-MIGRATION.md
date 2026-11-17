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

[Migration verification receipt](receipts/rc400-adapter-migration-20261116.json).
