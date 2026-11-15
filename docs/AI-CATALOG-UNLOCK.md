# Regular AI availability across editions

The native AI chooser now exposes all **14 regular opponents** in Gold,
Ubisoft History and Steam History. This includes Maid Marielle, Country
Bumpkin Rupert, Princess Loretta, Countess Emilia and Druid Mystizia, which
the retired profile-content filter could omit from catalog queries.

The patch changes only that filter's two-byte conditional branch. It first
checks the complete original method and exact instruction for the relevant
edition. The original `Visible` filter remains active: the 33 other declarations
are internal campaign variants or placeholders, rather than additional regular
opponents. It does not edit account data or award achievements.

Both ordinary startup adapters install this change after validating the original
executable. Unknown images or changed methods are rejected. New installer
artifacts must be rebuilt and verified by the release workflow; an existing
installed binary does not gain the change automatically.

## Verification

The actual DXVK pair tests used Maid Marielle and Field Marshal Ludowig, two
human clients and active native NEO FIFO/intent/priority support:

| Pair | Native catalog on both clients | Shared original CRCs | Differences |
|---|---:|---:|---:|
| Gold / original Steam History | 14 | 2,477, turns 0–2,476 | 0 |
| Gold / original Ubisoft History | 14 | 2,017, turns 0–2,016 | 0 |

Both tests exited normally and restored their profiles and original adapters.
The original profile declarations also match across all three editions.

The compiled integrity guard passed 15 offline checks against the pinned original
analysis images. Another 24 CPU cases execute the actual visibility and content
filter instructions with a modeled external-service result. Hidden entries stay
hidden even when the service reports a lock. These CPU cases are separate from
the actual game evidence.

`native/build_gold.ps1` builds `ai_catalog_unlock_tests.exe`. With locally owned
analysis images and the existing pefile/Unicorn environment, use
`tools/gold/verify_ai_catalog.py --help` to reproduce the offline checks. Keep
images, fixtures and outputs under ignored `work/`.

## Remaining qualification

The additional catalog audit found 28 skirmish and two scenario maps available.
Campaign metadata still marks 11 of 12 entries locked; campaign selection and
all cosmetic items have **not** been exhaustively qualified by this milestone.
Do not describe this as every unlockable item being verified.

This is also not proof of shared session reload. In the Steam test, independent
native saves landed at turns 300 and 301. In the Ubisoft test, both landed at
300 with identical NEO metadata. A coordinated checkpoint and actual native
shared reload remain required, as does full NEO5 qualification.

Machine-readable evidence: [AI catalog receipt](receipts/ai-catalog-20261115.json).
