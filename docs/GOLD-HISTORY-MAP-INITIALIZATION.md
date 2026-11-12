# Gold / History guest map initialization

The isolated Gold/History lobby bridge now changes a History guest's initial
Turminsel configuration to the Gold host's Monhaim configuration and retains
the two admitted human players. It waits for the game's configuration cache
before loading, then restores preparation data through original native
setters and owned event clones. Common world start, synchronized turns, Gold
NEO, customized templates and mixed saving/cold reload remain incomplete.
Ordinary adapter builds do not include these experimental components.

## Cause and ordering

Previously the bridge marked configuration receipt complete immediately after
enqueueing the original native configuration clone. The same-frame Lua guest
initializer still read Turminsel from `Network.GetMap()`. Its immediate
`MapSetup.GetCurrentMapData()` check also reported Turminsel, so that check
passed. In the following frame the original configuration consumer updated
the cache to Monhaim and the extension cancelled on the mismatch. Earlier
observations described this as map reversion; the added source/load/next-frame
trace established that the wrong map was being requested initially.

`native/crossplay_session_map_observer.h` separates enqueue success from a
matching native map postcondition. The caller retains `PendingHistorySessionMap`
and keeps the guest-presentation gate closed while observation is pending.
The observer requires the same owner thread, session, cache object, admitted
two-player guest roster, bounded relative host map and a ten-second interval.
It reads the actual cache without writing a map or acknowledging a simulation
event. `Applied` means this map postcondition matches; it does not prove that
every configuration field or event consumer completed.

After that gate, `tools/gold/history_guest_lobby.lua` captures the map and calls
the original loader once. `native/crossplay_history_map_reload.h` captures
owned original Confirmed events and castle designs before the loader rebuilds
preparation. It projects the backend room map through the original by-value
string setter, then restores the admitted rows and enqueues local clones.
The next-frame Lua checks and native postconditions must match before the
original guest-page callback runs. Session/map changes cancel this interval.
No repeated reload loop or Ready/Start click is supplied by the extension.

## Actual runtime checks

Two fresh DXVK High 1920×1080 pairs passed with different initial maps:
Gold with Ubisoft History data, and Gold with Steam History data. Both data
sets ran on the same fingerprinted isolated Ubisoft standalone executable;
no Steam SDK was loaded. This does not qualify the original Steam executable.

Read-only process inspections checked both actual preparation-map paths,
configuration caches, player IDs, assigned slots and the History backend room
descriptor. History displayed the original Monhaim guest page with both human
players and two open slots. Two real guest Ready-button clicks per pair then
produced 0 → 1 → 0 in the Gold cache/NetZ guest record and History cache/
preparation state. The extracted public observer passed an additional fresh
Ubisoft-data map/roster regression. All clients exited normally and diagnostic
options/adapters/archives were restored; saves, cloud and shared files stayed
unchanged. See the [sanitized receipt](verification/gold-history-map-20261112.json).

The existing fourteen Lua 5.1 tests cover one-shot load, reentry, invalid maps,
required native restoration, role/session changes and original callback results.
The native session-map fixture checks early ownership, deadline, edition and
path exclusions against deliberately unmapped addresses. These synthetic
checks are separate from the real client observations above.
