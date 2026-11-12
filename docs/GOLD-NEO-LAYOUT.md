# Gold logistics task layout audit

The Gold NEO port requires explicit native layouts. A constant four-byte shift
from History is incorrect: common base fields retain their offsets, several
derived fields move by four bytes, and later fields move by eight or twelve.
This audit supplies layout evidence only. Gold NEO hooks, shared NEO session
configuration, mixed gameplay and save/resume remain unqualified.

`tools/gold/emulate_logistics_fields.py` executes the original virtual
serializers of `ELLog::CRequestTask` and `ELLog::CProductionTask` in each of the
three fingerprinted Gold, Ubisoft History and unpacked Steam analysis images.
All six executions completed against a modeled recording visitor and reflection
name. The two History images produced identical observed fields and offsets.
The Steam analysis image is never launched.

Selected observed fields (hex offsets):

| Task / serialized field | Gold | Both History images |
| --- | --- | --- |
| Common base `A` | `04` | `04` |
| Request `a` | `4c` | `48` |
| Request `d` | `89` | `85` |
| Request `q` | `e4` | `d8` |
| Production `Running` | `58` | `54` |
| Production `Finished` | `59` | `55` |
| Production `OfferID` | `5c` | `58` |
| Production `BoostMultiplier` | `64` | `60` |
| Production `StartToRunTurn` | `7c` | `74` |

Letter tags above are the actual serializer tags, without assigning unproved
semantic names. Stack temporaries have `null` offsets in the fixture; no object
offset is inferred from them. Nested node bodies are deliberately skipped.
Containers are empty; HE request trees use owned, self-linked empty sentinels.
No nonempty container layout, task allocation size, ID resolver, allocator,
wire encoding, deserializer or active-world behavior is established by this
recording visitor.

The runner checks field order and visitor method type across editions, compares
the complete mapping with `tests/fixtures/gold-native-logistics-fields.json`,
and fails on changes. It bounds execution and writes, checks the thiscall
stack and nonvolatile registers, verifies the emulated SEH head is restored,
and requires the entire synthetic task and sentinel region to remain unchanged.
Image fingerprints are inherited from `emulate_event_payloads.py`; no host
native call, game process or network connection is made.

Run with private, owned analysis images and the existing pefile/Unicorn Python
environment:

```powershell
python tools/gold/emulate_logistics_fields.py --gold <gold-original.exe> --ubisoft <history-original.exe> --steam-analysis <steam-analysis.exe> work/logistics-fields.private.json
```

The output must be fresh and under repository `work/`. Images and runtime
receipts stay ignored. Reusing `Image(..., types=...)` keeps the existing event
payload oracle's default type selection unchanged.
