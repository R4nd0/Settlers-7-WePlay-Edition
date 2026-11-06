# RC3.17: native Linux preview

The installer, launcher, local service, archive compressor, cryptography helper
and managed game tunnel can run natively on Linux. The original History Edition
continues to run through Wine/Proton; each player supplies their own game files.

This release also updates the guarded adapter's Wine CPU-affinity and public-key
compatibility while retaining executable, callsite, object and cryptographic
validation. The network content contract identifies identical game files across
Windows and Linux while each host still checks its complete local inventory.
All participants need RC3.17, including Windows peers.

Actual Ubisoft History Edition qualification used Ubuntu 24.04 under WSL2/WSLg,
WineHQ 11 with a fresh win32 prefix and WineD3D. Solo quick/automatic save,
complete-process restart and cold load passed. Windows/Linux multiplayer passed
in both host roles, with NEO on and off, cold checkpoint resume and continued
save. Updating and moving the Linux copy preserved actual saves and timestamps;
the moved copy resumed the match. Across six complete replay pairs, all gzip
members contain 11,305 common native checksums with zero mismatches. A real
five-minute host autosave passed complete-process restart, native transfer and
resumed play on both clients. The user reported the extended match
stable until its end.

A reproduced WSL coordinator-lifetime defect is corrected: exiting the creating
launcher no longer kills the native service while Windows peers retain a lease.
The real native service/Windows lease check and automatic last-peer cleanup pass.
An earlier disconnect-related Windows abort preceded this correction; later
reverse-role and extended games exited Windows normally.

Native Linux service tests pass 8,951 checks; Windows passes 8,957. All 20 Wine
x86 boundary suites, 39 selected Windows native suite executions, 20 Linux host
boundary tests and five complete-replay parser regressions pass. Isolated
transport tests verify six allowed services, two denied ports and cleanup.

This remains a preview: Mint VM, genuine Linux Steam/Proton, graphical Polkit
consent, hardware Vulkan gameplay, separate-PC endurance and performance were
unavailable or untested. DXVK file installation/restore passed; default WineD3D
was used for actual gameplay. No game files, profiles or credentials are shipped.
Keep backups. See [Linux setup and measured limits](LINUX.md) and the
[verification receipt](verification/linux-20261106.json).
