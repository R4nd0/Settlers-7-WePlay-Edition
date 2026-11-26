# Diagnostic memory observation

On 2026-11-26 the user explicitly requested removing memory-triggered start
blocks and automatic stops, including all future diagnostic runs. The laboratory
entry points now use observation-only memory checks. Do not add private free-RAM
or commit-reserve gates to later diagnostic scripts.

`observe_host_memory` returns available counters or reports an unavailable
measurement. `DiagnosticSentinel` records the actual pressure, scan failure and
recovery flags without refusing startup or terminating an owned child. Graphics
laboratory reports continue publishing a fresh owner heartbeat when a monitor
has stopped producing samples. Report-publication errors are nonfatal. Capture
helpers accept pressure/failure observations while retaining owner attribution
and heartbeat validation.

The rule covers graphics experiments, renderer/math/device fixtures, original
water-program fixtures, the D3D11 experiment and the Lua/standalone debugger
starters. Stored diagnostic pressure does not select a different graphics recipe.
Production startup uses its separate policy.

Executable hashes, native ABI checks, ownership/isolation, one-game scheduling,
source fingerprints and restoration remain mandatory. Memory measurements retain
their real values; continuing a diagnostic does not make a failed or pressured
measurement healthy and does not establish release/performance qualification.

Validation covers startup under reported low memory, failed probe construction,
continued observation under critical pressure, no child termination on monitor
failure, truthful failure records, fresh failed-monitor heartbeats and nonfatal
report I/O failures. The focused suites passed77 graphics/capture tests,
27 memory-policy tests and5 D3D11-starter tests.

The first subsequent actual Ubisoft native-size case captured its original
1920×1080 source/output and a complete8672-draw frame, then closed normally with
exit0. Its observation recorded pressure2 and minimum host commit headroom595MiB
without a forced owner stop. Shared profiles and game binaries verified unchanged;
private options restored and cleanup errors were empty. These measurements remain
reported as pressured rather than being relabelled healthy.
