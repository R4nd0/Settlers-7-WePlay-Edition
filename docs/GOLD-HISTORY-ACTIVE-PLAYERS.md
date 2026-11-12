# Original active-player writer ABI

The experimental native component now calls the original active-player row
writers in Gold, Ubisoft History and the Steam analysis image. It is excluded
from ordinary adapters. This is an ABI primitive, **not** a complete live setup
capture, a start agreement or playable Gold/History crossplay.

The earlier [rule signature](GOLD-HISTORY-SETUP-SIGNATURE.md) writes template
players only. These newly audited methods instead visit player identities,
names, color references, population and economy fields, position/network slot,
state/type, access bitfields, blueprint lists, team/allies, difficulty, peaceful
mode and nested Castle Forge data. Other preparation metadata and dependencies
still require their own capture. Unknown native fields remain unknown.

`native/crossplay_active_player_writer.h` separates the edition-specific ABI
from method integrity checks. Gold takes the complete row-array base in ECX,
the visitor in EDI, and slot/version on the stack; History uses a thiscall
receiver and three stack arguments. Gold rows occupy 1,476 bytes, History rows
1,448 bytes. Two 12-KiB/8-KiB original code regions per profile are fingerprinted.
Executable/session/owner/visitor validation remains the future live caller's
responsibility; calling this primitive directly on unvalidated process data is
unsupported. No game adapter includes it or releases a held Start event.

The layout differences are not constant. For example, PrestigePoints is at
Gold `+0x4fc` / History `+0x4f0`, VictoryPointCount at `+0x54c` / `+0x53c`, and
Castle Forge strings begin at `+0x574` / `+0x560`. Blueprint/alliance list
headers differ too. State, Type and DifficultyLevel are passed through stack
temporaries; the recording visitor does not invent their original offsets.

## Offline verification

`tools/gold/emulate_active_players.py` runs the **compiled** x86 call thunks
against each fingerprinted original image. Each format version 1–9 has empty
and populated cases, each run once with a recording visitor and once with the
original binary visitor: 108 original row executions in total. All corresponding
Gold/History field schemas and binary outputs agree for these synthetic inputs.

Populated cases include two trade options, rewards, both blueprint lists and
allies; three Untouchable entries where supported; a named Castle Forge design
with two parts/parents/slots; a nonzero symbolic color and player/scalar values.
ProfileAccess and BlueprintAccess exercise bit boundaries across native 32-bit
and 64-bit words, including the final bit. The original expansion into 512/256
bytes executes; only allocation/free and the bounded output stream are modeled.
Nested nodes execute, and list heads, color tables and pointed-to strings are
valid owned fixture objects. This is more specific than treating zero pointers
as empty native containers.

Checks preserve the stack, nonvolatile registers, SEH head, row-array storage,
pointed-to fixture data and visitor error flags; reject object/global writes,
unexpected null-page reads, unowned frees and unbalanced scopes; and bound CPU,
heap and stream sizes. The compiled code guards pass 24 checks covering original
bytes, mutations at both ends of each region and inaccessible regions. The
shared guard emulator retains its old instruction budget by default; this
larger diagnostic explicitly selects a bounded two-million-instruction budget.

Reviewed field metadata and synthetic output hashes are in
[`tests/fixtures/gold-native-active-player-fields.json`](../tests/fixtures/gold-native-active-player-fields.json).
Build the fixed analysis fixture with `native/build_crossplay.ps1`, then supply
it through `--thunk-executable` to the audit tool together with all three owned
analysis images and a fresh receipt path under `work/`. Never launch that fixed
analysis fixture as a game or ship the private game inputs.

These runs use one synthetic row, including nonzero slot selection, rather than
an actual 25-row native lobby. They do not test original save readers, complete
setup/bootstrap/content agreement, common world start, turn synchronization,
Gold NEO or mixed saving/cold reload. Original callers using format version 8
were located in Gold and Ubisoft History, but the live fresh-start serialization
path has not been qualified by these checks. No game process was started.

Sanitized results are recorded in
[`verification/gold-history-active-players-20261112.json`](verification/gold-history-active-players-20261112.json).
