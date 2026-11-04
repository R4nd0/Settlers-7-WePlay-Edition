# Resumed multiplayer: native AI terrain-search stall

The saved 2-human/2-AI match could render its world and then stop progressing.
The host saturated its main thread while the guest waited for it. Checkpoint
transfer had already finished. An older diagnostic run showed the same search
loop before the optional transfer-pacing change existed; repeat runs with both
pacing policies also progressed. The stall was intermittent.

## Confirmed failure mechanism

The stalled AI footprint search received an enormous finite binary32 angle in
degrees. Its native rectangle constructor converted this to radians and called
x87 sine/cosine helpers outside their supported reduction range. Those helpers
left the huge argument unchanged. The resulting corners were around 10^29 world
units; conversion to raster coordinates overflowed and the terrain traversal
scanned an enormous range on the host's main thread.

The captured angle is approximately 1.212669e28 degrees. Its exact binary32
integer value modulo 360 is 24 degrees. Why the AI produced that input remains
unproven; this correction prevents the demonstrated geometry/search failure.

## Correction

`terrain_angle_bridge.inc` guards only the identified footprint-constructor
callsite in the local multiplayer path. Finite angles with magnitude at least
2^63 degrees are reduced modulo 360 using integer arithmetic on their IEEE-754
representation. Every float in this range is an exact integer, and reduction
takes at most 104 bounded steps. Ordinary angles retain their original bits.
NaN and infinity pass through unchanged.

The x86 bridge preserves general registers, flags, the original stack and
callee cleanup, LastError, x87 state and every SSE lane except the selected
angle's low float. It tail-jumps to the original constructor. Executable hashes,
edition-specific callsite bytes, initialization, owner-thread and local-connect
guards remain in place. It does not skip AI, fabricate readiness or suppress
native desync checks. A fixed status label records its first actual reduction.

Ubisoft and Steam use separately verified address maps and original call bytes.
This correction does not enable the optional `-ResumeCatchup` pacing candidate.

## Deterministic verification

The native test reproduces the original x87 out-of-range result without running
the huge raster loop, then verifies bounded rotation from the reduced angle.
Both edition mappings exercise the actual bridge against a synthetic native
constructor, including callee-cleaned arguments, stack canaries, register/flags
sentinels, LastError and complete x87/SSE state preservation. A deliberately
clobbering helper exercises the slow path. Wrong owner, disabled probe and
incomplete initialization leave inputs unchanged. Numeric fixtures cover both
signs, every large finite exponent, sampled mantissas and special values.

The lab and release profiles each pass 34,444 terrain-angle checks and the
existing selected native suites. The compatibility profile without the network
pump also builds and passes its 33,294 numeric checks.

## Actual two-client verification

Two cold-started isolated Ubisoft clients resumed the original checkpoint with
two human slots on team I and two normal AI opponents on team II. The guest
selected Ready once. The host's real transfer queue drained after 167.140 seconds
and Start enabled automatically. This run used the normal pacing policy.

The guard actually reduced an extreme angle on the host during this run. Both
worlds continued with matching turn counters; the guest's castle inspection
and the host's unit inspection remained responsive. The host saved automatically
at world turn 15003, 3000 simulation turns after the restored checkpoint. The
new save retained roles `[human, human, AI, AI]` and teams `[I, I, II, II]`.
The guest's autosave remained unchanged. Both worlds advanced past the save.

The user closed the two test windows. Both processes returned exit code 0. Their
complete native replay streams contained 3924 common checksum records, all
matching (3924 host records and 3925 guest records). Both memory sentinels stayed
healthy; minimum free host RAM was 5033 MiB. The generated checkpoint, profiles
and recordings were preserved privately. Original test adapters/UI and the
input checkpoint, profile index and timestamp were restored. No original game
installation was modified.

This verifies the observed resumed-match failure path on two local Ubisoft
processes through the next autosave. Steam received synthetic map/ABI checks,
not a new live-game trial. Separate PCs, Internet play and extended stability
remain outside this run's evidence. The upstream source of the invalid AI angle
and unrelated desync causes are not established by this correction.
