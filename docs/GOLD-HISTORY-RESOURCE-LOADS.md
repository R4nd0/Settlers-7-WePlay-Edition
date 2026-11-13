# Native resource-load investigation

Gold and both original History engines now pass the independent-target
[setup reader](GOLD-HISTORY-SETUP-ROOT.md). A common root serialization still
does not prove that each engine loads the same simulation resources or executes
their startup scripts in the same order. Shared world permission remains closed.

Read-only inspections of the owned Gold/Ubisoft processes and the original Steam
process independently confirm the primary file-system collection, all ten
virtual slots and the full original Open method fingerprint. The method uses
`ECX=this`, two stack arguments and `ret 8` in all three images. Its optional
native callback observes requests before resolution, so it cannot establish
successful reads. No resource observer is installed by these inspections.

`crossplay_resource_open_bridge.h` provides an opt-in passive forwarding
primitive. It installs nothing and dereferences no path. A guarded integration
must still bind the original target, verify object ownership and bound/scope any
borrowed path before observation. The primitive restores incoming registers,
flags, LastError and x87/SSE/MXCSR before tail-forwarding to the original method.
The original method determines results, error status, exceptions and cleanup.

The compiled bridge passes 36 bounded CPU cases across the three fingerprinted
original Open implementations: direct baselines and wrappers with observation
off/on, covering ordinary resolution, fallback, missing files and directories.
Child filesystems and Windows error APIs are modeled; actual original Open bodies
execute and every wrapper must match its direct baseline. The observer deliberately
clobbers floating-point state and LastError. A separate Windows fixture also
raises an actual structured observer exception and verifies original forwarding.
No resources are read and no game is launched by these fixtures.

The next runtime observer must report its actual scope and any dropped, missing
or unclassified observations. Request logs alone cannot authorize the world or
replace a mounted-content fingerprint. In particular, Gold's additional explicit
load of the otherwise identical `global/start.lua` remains an unresolved startup
difference. These results do not qualify shared world, Gold NEO or mixed reload.

See the [forwarding ABI receipt](verification/gold-history-resource-open-bridge-20261113.json).
