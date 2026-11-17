# Original History host START capture

The original Steam History host now has a separate, opt-in capture-only
primitive in `native/crossplay_history_start_hold.h`. It follows the existing
Gold hold contract. It does not add ordinary reverse-role admission, replay an
event or grant world permission. See the
[component and actual-pair receipt](receipts/native-history-start-capture-component-20261117.json).

The helper pins the complete original 208-byte START producer and owns only
its five-byte virtual-send callsite. Its entry replays the original sender load,
preserves the original continuation and five arguments, and returns with the
original 20-byte argument cleanup. The bridge preserves general registers,
flags, x87/SSE state and LastError. The borrowed original event must be copied
and validated synchronously before the original caller destroys it. The general
event sender remains unchanged.

Installation requires the actual admitted original Steam host/Gold guest
roster, owner thread, native preparation/cache/transport roots and matching
original methods. Capture additionally requires the same native session,
transport and cache, actual selected fresh map, empty save token and both native
human Ready bits. It permits only one attempt. Release restores only its exact
owned jump on the owning thread.

Original START UI actions can enqueue closed-slot changes before producing the
start event. Therefore capture does not assume that the earlier preparation
root or slot count survives that action unchanged. Complete neutral/AI setup,
fresh checkpoint/root equality and world permission remain the responsibility
of the existing later barriers. Capturing an event would not establish any of
those conditions.

## Verification and current exclusion

The ten reverse component fixtures pass. The new component contributes 19
authored ABI/scope checks. Against the independently pinned original History
image, 29 compiled original-code guard cases, 29 exact owned-jump normalization
cases and two compiled continuation-entry cases pass. Those guard/entry checks
execute fixture code only, not original game code or a world.

Two actual original-game pairs installed the hold before Ready and repeated the
genuine native Ready exchange, standalone under DXVK 3.1.1 at normal pace.
Gold's already checked DLL and archive were reused exactly. Both clients exited0
in each pair, with strict guarded-file, profile, auxiliary-file and service
restoration. Neither pair recorded the original START UI action or a captured
start event. They do not qualify reverse world start, NEO or History-origin
save/reload. Each recorded one broker rejection whose exact cause was not
captured; it is not classified as a proven teardown-only rejection.

The second pair reused the existing original cached host-lobby refresh, but its
new exclusion reports exceeded the console command's existing 80-byte bound.
Absence of those reports does not identify the native or UI cause. Compact
reports now separately cover the original host page/role, native capture
availability and actual original widget conditions. The 80-byte limit remains
unchanged. All 568 candidate Lua members parse, unchanged archive payloads are
compared, and yielding/reentrant original refresh/START callbacks are checked
offline. The compact candidate has not run: the unchanged 7-GiB startup guard
excluded it before either game started. Guarded files were restored.

The prepared controller retains full asset hashes and the same RAM threshold
before deployment and immediately before launch. A bounded idle interval after
hashing can wait for that original buffer; file metadata and candidate hashes
are checked again afterward. No native lease or timeout is extended. The next
actual pair must identify the original START exclusion and capture the real
event before the existing setup/root/world pipeline is connected.
