# Multiplayer checkpoint integration

Owned Lua components for RC3.11 multiplayer save/resume.
They require the matching guarded native save-transfer implementation and UI
patch recipe. Do not copy a Lua file alone into an installed game.

`runtime.lua` records the actual lobby roster, validates and encodes it as data,
embeds it in the native save cache, and maintains a profile index bound to the
slot's name, date, map and native map path. `session_list.lua` extends the
existing session browser to select a checkpoint and restore that roster.
Unknown saves are rejected. Map Forge checkpoints retain the saved template
name when loading setup data and creating the native resume lobby. This also
requires the native template-name lifetime correction included in RC3.16. The host's
template must still exist under the saved map; an absent template is rejected
before creating a lobby. The received-world slot 1000 is never indexed; native
autosave 1001 and manual slots 1002..1009 are supported. See the
[variant checks and their limits](../../docs/MULTIPLAYER-MAP-VARIANTS.md).

The host autosave scheduler uses a separate 300000 ms game-time interval. Manual
saving does not postpone it. Starting or destroying a world resets the timer;
a backwards clock resets it, and missed intervals never produce a catch-up
burst. The integration must pass eligibility only while the world is started
and not disconnected, desynced, exiting or defeated. The scheduler additionally
requires multiplayer mode, a live session, host ownership and local player 1.
Its callback is the original game autosave function, preserving the native
save path and separate automatic slot. Native errors are not suppressed.
Singleplayer retains its original autosave condition.

Run `python -B tools/multiplayer-resume/test_runtime.py` with Lua 5.1 support
available through `lupa.lua51`. The tests validate metadata and scheduler
behavior against explicit API doubles; they do not establish native transport
or synchronization correctness.
`python -B tools/multiplayer-resume/test_session_list.py` also exercises the
actual browser callback's variant validation and deferred resume path.

`interface_patch.py` builds the UI archive from the exact known
WePlay archive fingerprint. It verifies every archive member after replacement
and never overwrites an input installation. `host_lobby.lua` supplies the
deferred lobby setup hook and refreshes cached readiness while peers are not
ready. The guarded native adapter holds readiness while its actual
save-transfer queue is pending. Human Ready alone does not establish transfer
completion. The functional transfer/readiness hooks are built with
`LocalNetworkPump` for both original executable profiles; laboratory tracing
remains separate.

Observed native verification on 2026-11-02: a manually saved 2-human/2-AI match
was resumed after both processes fully exited. Both original human roles and
AI parameters were restored, and ten minutes of continued play produced 6438
common native replay checksums with zero mismatches. The native embedded
metadata exists on disk, while its availability through the post-load cache
API is still unresolved; this test restored the roster through the persisted
profile index. Intermittent desyncs on an older checkpoint remain a separate
unresolved issue.

The native automatic slot omits its name. The runtime normalizes that absent
name only for slot 1001, validates encoded metadata before indexing, and reports
cache/index failures. Native acceptance on 2026-11-03 verified two actual
metadata-containing autosaves five minutes apart and a full-process restart:
6830 common CRCs matched before the restart and 7112 after it. A separately
created Steam-host/Ubisoft-guest match also saved and continued after both
processes exited; 3860 common CRCs matched before the restart and 4291 after it.
The resumed Steam host produced another actual five-minute autosave. Guests'
automatic slots remained unchanged, and all four runs exited normally.
Both editions held early starts until the actual sender drained and refreshed
the UI without a second Ready click. See the [release qualification](../../docs/RC3.11-MULTIPLAYER-SAVES.md)
for laboratory identities, memory-monitor gaps and remaining limits.
