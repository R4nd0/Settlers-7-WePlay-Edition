# Generated shader cache lifetime

The native22 Ubi 4K observation showed approximately three shader COM
creation calls per active frame. That counter describes CreateVertexShader /
CreatePixelShader calls; it does not establish repeated driver compilation.
The following ownership split is implemented in candidate23 and has CPU/real-GPU
fixture evidence. Its actual game pacing and exit behavior remain separate tests.

## Minimal ownership split

Keep two bounded ownership groups:

- Original-program classification references: at most the existing frame-cache
  limit. Release them at EndFrame to preserve the current original resource
  lifetime and pointer-reuse guarantees.
- Generated program objects: at most one leaf VS and two terrain PS objects.
  Retain them between frames and create each lazily when its role is needed.

The immutable recipient-generated CPU program bytes and file leases remain
separate from these COM objects. No private device reference counters, object
fields or compiler-private slots are needed.

| Boundary | Classification refs | Generated GPU programs | CPU programs/file leases |
| --- | --- | --- | --- |
| EndFrame | Clear | Retain | Retain |
| Every external device Release | Clear | Clear | Retain |
| Actual Reset, including a failed Reset | Clear | Clear | Retain |
| Explicit disable/subset change | Clear | Clear under the existing conservative contract | Retain |
| Full reconfiguration/shutdown | Clear | Clear | Close/reload as appropriate |
| Unrestored transaction | Do not invent cleanup as restoration | Stop unsafe drawing; retain the established fail-closed handling | Do not re-enable |

The integrating hook already calls the component Reset before forwarding every
external device Release, inside its serial device lock and internal/TLS scope.
Keep that rule unchanged in the first optimization. Releasing a child shader can
release its parent device; nested callbacks must continue through the internal
forwarding path. COM Release's numeric result is not evidence of object death.

This minimal split does not depend on a final-Release guess or a new cross-frame
device lifetime policy. It may recreate programs after real external references
are released, but removes unconditional frame-end churn. A more aggressive
window-lifetime cache is a separate change requiring independent teardown proof.

## Verification after the live case closes

Before changing or deploying a loaded module, close the owned game and retain
its source checkpoint, profile restoration and current evidence.

The CPU transaction fixture should verify lazy creation once per role across
multiple frame boundaries, bounded retained program references, exact restoration
before EndFrame, and ClearAll at disable, external Release cleanup, actual Reset
and shutdown. It should preserve counters, LastError and floating-point state.

The real GPU fixture should repeat an enabled role over several frames with no
intervening external Release, verify stable creation counts after warmup, then
exercise Reset and disable/re-enable with lazy recreation and identical output.
The actual game still needs separate on/off pacing observations without GPU
readback captures. Do not treat fewer COM creation calls alone as a measured
frame-time improvement or extend one edition's result to another.

## Candidate23 lifecycle observations

The native22 actual case reached its final cleanup report and then exited with
access violation `0xc0000005` after the original UI exit confirmation. Profiles
and original binaries were restored. No matching fault-module/stack evidence was
available, so the report alone does not establish the cause.

Candidate23 adds an earlier cleanup boundary at the original common vBase
`VVideo::DeInitializeD3D` / `DeInitializeScreen` exports imported by Vision90.
It checks exact file identities, export RVAs, ASLR-normalized prologues, original
IAT values, caller module and owner thread. Window-destroy fallback now forwards
the original callback before releasing its last cached child and defers cleanup
through nested callbacks or an in-flight COM call.

GPU fixtures passed with a held parent reference, with the output swapchain as
the last public parent owner, and with an explicit guarded pre-deinit boundary.
The original destroy handler could still query the live device. Wrong-thread and
foreign-caller requests were rejected. A separate warm-frame fixture retained a
generated shader over eight frames without additional creation calls, then
passed Reset and disable/re-enable. These tests do not prove the actual exit AV
is fixed; a new real load/save/exit result is required.

### Limited actual Ubi retest

The first candidate23 game retest ended before the Exit action because the
existing memory guardian reported a measurement `scan_timeout`. That interrupted
case establishes neither an exit AV recurrence nor a successful lifecycle test.
It recorded 12 shader creation calls over 9,720 frames with three generated
programs retained; these cache counters are separate from frame-time or lifecycle
qualification.

The following owned Ubi History retest loaded a newly created private checkpoint,
completed another new private save, then used the original UI exit path and
returned exit code 0 without forced termination. Final native counters reported
two guarded engine-deinit calls, zero rejected calls, one early cleanup and two
original deinit returns. Cleanup completed before the window-destroy fallback
was needed (`window_shutdowns: 0`, `terminal_cleanup: 1`, `hook_detached: 1`).
The memory monitor remained healthy with pressure 0; private options/profiles,
shared profiles and verified game binaries were restored unchanged.

This is one successful actual Ubi load/save/exit observation for the exact
candidate23 checkpoint. It supports the earlier engine-deinit boundary on that
path, but does not identify the native22 crash's faulting instruction, prove its
root cause, establish repeated-session reliability, or qualify Gold/Steam exit.
