# Complete player setup transport

The experimental mixed-edition admission protocol now preserves both original
CastleForgeData strings. The old cosmetic `castle_wire::Design::type` represents
the native serializer's first string, `Name`; it does not represent the second
string, `Type`. The separate bounded `CrossplayCastleSetup` envelope carries both
without changing the existing cosmetic wire format. Admission protocol version 3
rejects version 2 instead of accepting a peer that loses the second string.

Original Confirmed constructors and local queue clones preserve the complete
setup. History map reload and Ready updates also use the complete value. Private
diagnostic preparation and own-player restoration must use these same APIs;
rebuilding a legacy cosmetic value before admission would already erase Type.

`crossplay_closed_preparation.h` supplies an opt-in projection after the original
closed-slot cache consumer. It applies only to unused Monhaim positions 3 and 4
in an owned fresh two-human History lobby. Original Name, NetworkSlot and Type
setters clear the old AI name, assign the closed position's network slot, and
set its native neutral type. Independent executable-specific method pins cover
both History binaries, including the complete empty-string helper. Ownership,
closed native cache, row state, bounds, writable regions, and before/after
preservation remain checked. It does not synthesize a world-start acknowledgement.

The offline original-code audit covers 328 constructor/destructor and local queue
cases across Gold and both History profiles, including nonempty values in both
Castle strings. The original closed-row setter audit passes 50 cases and 28
integrity checks across both History profiles. It executes 48 setter calls and
checks stack, registers and preservation outside the three intended fields.
Heap, locks, test rows and Windows memory queries remain modeled where stated.

The real DXVK High pair with Steam game data and an isolated Ubisoft executable
confirmed native two-human admission, original map reload and Ready delivery.
Its first correction removed the Name/NetworkSlot differences in positions 3/4.
Full version-8 row hashes still differed at positions 1, 3 and 4: the private
pre-admission rebuild still erased Castle Type, and closed History rows retained
human Type instead of neutral Type. The next immutable candidate fixes both
paths and builds successfully; its repeat pair has not run because the unchanged
4-GiB RAM preflight rejected the launch. Both clients from the completed pair
exited normally and options, saves, cloud data, adapters, interface and shared
profiles were restored or unchanged.

The corrected immutable repeat pair now confirms **all 25 version-8 rows equal**.
The complete original preparation writers also produce identical 32,236-byte
streams with SHA-256 c7070c16216a2483da1b222ca14f121ec0bbce05c91b99bb6697e0fbd7485494.
A separate read-only field/enum comparison agrees. Both clients exited normally
with all restoration checks passing. This supersedes the first pair's remaining
three row differences and its earlier RAM-blocked repeat.

The Steam-data pair is still
**not a native Steam-executable pair**. Version-8 preparation diagnostics do not
qualify the original enclosing setup's nested version-1 format. Native setup
reading/content agreement, shared world start, synchronized turns, Gold NEO and
mixed save/cold-reload remain unqualified.

A further corrected pair with Ubisoft game data now also captures the original
nested **preparation format 1**: both clients produce 31,059 bytes with SHA-256
`871ea11e5031bbee1107634397f312e1da376f17171e83f909bff4df2d02bd9b`.
Its version-8 preparation and all 25 version-8 rows agree too. The capture API
accepts only the qualified diagnostic versions 1 and 8, labels the captured
format, and rejects unsupported versions before native calls. Dependency,
owner, position-list and method guards remain in both paths; 42 dependency and
failure-path checks pass. Both clients exited normally with all restoration
checks passing. This qualifies the observed preparation streams, while the
enclosing rules/root reader and complete content agreement remain separate.

See the [sanitized verification receipt](verification/gold-history-player-setup-20261113.json).
