# Original UP draw observation

The native diagnostic observer also covers the standard D3D9
`DrawPrimitiveUP` and `DrawIndexedPrimitiveUP` methods at SDK slots 83/84.
They join the existing guarded class-table patch/rollback/cleanup list, for
nine intercepted slots. The original object vptr, private RTTI and unrelated
slots remain intact. Exact executable, renderer, module, ABI and device/window
guards still apply, including only the four identified Apphelp shim slots.

This path is read-only. Only a successful nonzero eligible original draw on
the admitted device, owning render thread and known game caller invokes
observer kind 3/4 inside the existing serial/TLS scope. Vertex/index pointers
and all scalar arguments pass to the original exactly once. The hook never
reads, copies or rewrites their buffers, and leaves the original UP stream and
index reset side effects intact. It performs no material replacement, high-scene
draw counting or material-epoch latch. An unrelated UI draw therefore cannot
qualify a newly configured material image.

Missing/foreign device context releases the host lock before forwarding. A
known-device foreign caller/thread or shutdown context forwards within internal
TLS without observation. Any pre-existing TLS scope bypasses the nonrecursive
lock, also for a different device; both cross-device reentry directions are
covered. Owned unsafe state blocks like the regular draw paths. The original
post-call FP/LastError state survives both observation and forwarding branches.

The seven `up_*` statistics describe hook calls, observer invocations, context
rejections, blocked calls and exceptions. An observer invocation does not prove
a known shader match. A UP call may bind null VS/PS, leaving no compiled program
pair. Neither a missing pair nor use of a UP method establishes that a draw is
UI or responsible for the observed black streaks. Actual pass attribution still
requires source-bound game observations.

`native/native_diagnostic_host_build.ps1` builds fresh immutable source snapshots
under `work/` with strict x86 `/W4 /WX /fp:strict`. Its authored CPU fixtures
exercise integrated UP forwarding, exact arguments, invalid pointer sentinels
(no hook-side dereference), original stream/index resets, observer kinds,
FP/LastError, failure/exception paths, unknown/foreign contexts, reentry,
unsafe blocking, absence of a material latch and exact nine-slot restoration.
CINTERFACE assertions bind UP slots/stdcall signatures and the fake shader
query slots directly to the Windows SDK. The diagnostic host fixture separately
checks AO intent/queue/frame/teardown contracts. These CPU checks create no
D3D device, game, window or attach-by-PID interface and do not qualify driver UP
rendering or actual Grass/GUI/Blob pass identity.

The independently reviewed private UP checkpoint passed 75 integrated and
20 protocol/SDK checks. The private AO-on-UP host checkpoint passed 203 checks.
After promotion, fresh public-source build receipts govern these results; old
private checkpoints do not qualify a changed full native build or release.
