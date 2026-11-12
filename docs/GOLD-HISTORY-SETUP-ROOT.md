# Original enclosing setup writer

`crossplay_setup_root_writer.h` adds the guarded original enclosing writer for
Gold and both independently pinned History binaries. It serializes the global
rules, optional RNG seed and the active multiplayer preparation. This differs
from the earlier template-player signature, which never represented the full
active roster. The helper is excluded from ordinary adapters and is not wired
to native start delivery.

Gold receives the root in EAX, visitor in ESI and format version on the stack
(`ret 4`). Both History profiles use `thiscall(root, visitor, version)` with
`ret 8`. Integrity checks cover the enclosing method, rules, complete player
writer, individual row writer and nested Castle writer. Unsupported editions,
unreadable regions and changed code remain rejected.

The original root writer's nested player nodes select **preparation version 1**
in every tested root format. The earlier live version-8 diagnostics therefore
remain a separate compatibility probe. They do not establish the native world
setup format. Root format 2 serializes the RNG seed through a temporary and
writes the unchanged value back to the root. The offline audit permits only
that exact write and checks the complete root and pointed-to input afterward.

The compiled x86 fixture passes 24 enclosing writer calls, including 24 complete
preparation calls and 600 row calls. Both root formats (1 and 2), empty/populated
player rows, populated rule strings, all nested player scopes and both position
lists produce matching schemas and bytes across all three editions. The audit
checks the nested version and row order, exact binary-writer storage/canaries,
input preservation, stack and nonvolatile registers. All 51 integrity checks
pass, including code mutation and unreadable-region rejection. The existing
row regression also passes 108 calls and 24 integrity checks against its
unchanged reviewed fixtures.

Victory and sector rule vectors are empty in these fixtures. Their nonempty
bodies and dependencies still need independent verification before live root
capture. No remote setup reader, content closure, shared world, synchronized
turn, Gold NEO or mixed save/cold-reload is qualified by this offline ABI audit.

See the [sanitized verification receipt](verification/gold-history-setup-root-20261112.json).
