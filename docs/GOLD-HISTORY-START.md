# Gold / History native fresh-start primitive

The opt-in native start component now validates and transports the original
`CGameStartEvent`, and supplies guarded local factory/clone delivery. It is
excluded from ordinary adapters and is not wired into the lobby-only runtime.
No mixed world has started. Complete actual content/template/rules agreement,
native queue-consumer/world transition and synchronized turns remain open.

`native/crossplay_game_start.h` accepts the original class `0x8bf00ae9`, event
type `0x00070007`, numeric host CID, asynchronous flag one and empty fresh-game
path. Resume paths are excluded. Its fixed-size frame binds reciprocal channel
identities, setup revision and a nonzero setup digest. Only the admitted host
CID changes between namespaces. This digest must come from independently
compared actual content and complete native setup; assigning matching values
does not establish agreement. The existing lobby test's synthetic content
digest must never authorize this path.

`native/crossplay_game_start_codec.h` retains the bound executable/session
guards and separately pins all three native event tables and method prefixes.
The source must be a genuine native event. Both admitted human rows must
actually be Ready, map/save cache fields must match the fresh-game interval,
and other slots must be closed or inactive. Factory output must consume the
entire validated input and reserialize identically before the original
local-only send clones it. The factory-owned temporary is then destroyed.

The receiver records its native session, owner thread, routing bindings,
reciprocal peer, setup agreement and map. It permits one delivery attempt in a
ten-second interval; invalid input or a native failure also consumes that
attempt. A caller must cancel the receiver if its complete setup/content
agreement changes. Successful enqueue is not evidence that a world started.

## Verification

The Windows x86 `/W4 /WX` build and 264 protocol/binding exclusions pass.
An independent constructor/writer fixture checks the actual event type; the
older scalar payload comparison did not determine constructor semantics.
The compiled integrity oracle passes 69 original-image/mutation/unreadability
checks across Gold, Ubisoft History and Steam History analysis profiles.

`tools/gold/emulate_game_start.py` also executes the original constructor,
class getter, writer and reader visitors, clone/local asynchronous enqueue and
destructors for three routing values per profile. All nine cases retain stack,
nonvolatile registers, SEH and bounded ownership, and release owned allocations.
The resulting fresh-start bytes match across editions. Heap/lock/stream imports,
reflection names and empty queue storage are modeled. No native factory,
queue consumer, real client/world or network runs in this emulator.

The new high-level factory/receiver path has not yet been positively exercised
in a real process. These checks do not qualify a common start, Gold NEO,
customized multiplayer templates or mixed saving/cold reload. See the
[sanitized development receipt](verification/gold-history-start-20261112.json).
