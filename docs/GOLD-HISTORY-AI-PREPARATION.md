# Gold ↔ History four-player AI preparation

The experimental native laboratory now starts Monhaim Valley with two human
clients and two AI players in both Gold/Steam History and Gold/Ubisoft History
pairs under DXVK. The bounded runs retained the original checksums and delivered
both humans' normal building and street commands. This is a local diagnostic
milestone. NEO, mixed save/reload, additional maps, effective x20 throughput and
an actual EasyTier game remain unqualified.

The subsequent 180-second Gold/Steam run reproduced a native desync: the first
1,278 finished turns agreed, then checksums differed at turn 1,278. Both clients
sent their original desync events. The cause remains unresolved; the shorter
passing runs below do not establish sustained crossplay stability.

## Missing native state

The original AI profile event reaches History's session configuration, where
its consumer selects CPU state. It does not populate the complete preparation
row. A passive runtime comparison showed History retaining unassigned network
slots, initial AI kind, default difficulty and a different second profile.
Gold had already applied its original host chooser settings.

`crossplay_ai_preparation.h` adds a separate 92-byte preparation record after
the two original AI events. It binds ordered slots 3/4 to the authenticated
channel's two peer IDs, the original event's profile, actual host difficulty
and peaceful flag, and a digest of the original AI name/profile definition.
The native receiver checks that digest against its own bounded original
catalog before changing its owned preparation row. HMAC authentication remains
the existing channel's responsibility; this record supplies no authentication
or start consent by itself.

`crossplay_ai_preparation_codec.h` calls seven original History functions for
kind, profile, name, profile affiliation, network slot, difficulty and peaceful
flag. Both History editions have separate method addresses and complete byte
guards. The profile callback is also guarded in full to pin the catalog layout.
The code checks native session/cache ownership, the real human roster and
Ready state, and the active preparation owner before and after applying it.
It checks the resulting fields and name and preserves every other byte in
all 25 preparation rows. No human slot or identity is taken from the AI record.

`crossplay_ai_preparation_source.h` retains the passive, guarded original
getter and writer views used to establish those layouts. It changes no game
state. `PreparationScope` permits a fresh typed eligibility check only for
the same thread, binding, session and two AI records. It supplies no world
permission. Ordinary adapters do not include these experimental headers.

## Start agreement

The private integration uses a distinct AI setup path and its own signature
flag. It does not pretend that AI slots received closed-slot events. The
existing checkpoint/nonces, fresh local ownership, all 25 original row writers,
complete version 1/2 setup streams, original independent reader/writeback,
content contract and mutual start barrier remain required. Original RNG,
load-progress, EOT and desync checks remain active.

Both real pairs agreed on the complete original setup before starting. They
used the original loader, which closed normally without hiding the UI by force.
Both accepted the original replay-duration request of 5 ms. That readback does
not demonstrate twenty times normal simulation throughput.

## Verification, 2026-11-14

| Actual pair | Equal consecutive original CRCs | First/last finished turn | Real commands delivered |
|---|---:|---|---|
| Gold / Steam History | 576 | 0–575 | One building and one street per human |
| Gold / Ubisoft History | 443 | 0–442 | One building and one street per human |

The two additional preparation-only tests established actual setter/readback
on both History editions; the Ubisoft test also compared every original row
and the full 33,252-byte enclosing setup. The later world-start pairs performed
their own fresh complete setup agreements. No desync was sent during either
bounded world run. This does not establish sustained AI behavior or building
completion. Each broker recorded one `ConnectionResetError` during normal
watchdog shutdown; zero-reset shutdown behavior is not claimed.

All clients exited normally, and options, profiles, adapters, archives and shared
state were restored. No preexisting save was replaced. The original installations
were preserved; no remote PC was operated.

The extracted public code passes 1,606 framing, ordering, namespace, field-boundary,
scope-default and preservation checks under the x86 compiler with warnings as
errors. A separate bounded CPU fixture executed the six scalar original setters
84 times across both History analysis images and verified exact writes, stack
cleanup and nonvolatile registers. The fixture used synthetic rows; the actual
game runs provide the runtime evidence. All three extracted native candidates
also pass their existing observer ABI checks.

The extracted public headers were then used in that longer real Gold/Steam
run. It compared 1,286 original CRCs through turn 1,285, with two mismatches
beginning at 1,278. NEO was off. An added read-only Lua world-statistics observer
did not produce qualified world samples, so it supplies no AI-activity evidence.
The original 180-second watchdog closed both clients normally and all scoped
files were restored. No UI input or forced process termination was needed.

See the [sanitized verification record](verification/gold-history-ai-preparation-20261114.json),
[original AI event bridge](GOLD-HISTORY-AI-EVENTS.md), and
[EasyTier and AI scope](GOLD-HISTORY-EASYTIER-AI.md).
