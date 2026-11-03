# Preserve options during startup

RC3.13 answers S7's “Startup problem detected!”
reset-options question with **No** automatically. Existing settings are retained,
including when the preceding process was stopped. The RC3.12 downloads have not
been replaced by this change.

The original startup check tests Shift and `Startup.tag`. It can request an
options reset even after successful menu use: in the examined Ubisoft image,
the marker cleanup is called at map-load completion, while WePlay can start
with `-noMapInMainMenu`. A marker therefore does not prove the menu failed.

Only the exact reset-question callsite is changed in process memory. The game
still parses its existing options and follows its original No continuation.
The replacement returns false in AL and discards the original dialog's one
callee-cleaned stack argument. The options file, marker lifecycle and other
dialogs are untouched. In-game settings and launcher graphics controls remain
available for deliberate changes.

The Ubisoft callsite is `EC698B` (original target `4E7B90`); the independently
mapped Steam callsite is `EC6076` (target `4E7930`). Both genuine image layouts
have the same boolean consumer and a `RET 4` dialog epilogue. Each build retains
its own exact original instruction guard, executable identity and patch rollback.
No game EXE is changed on disk.

Verification on 2026-11-03:

- Executed the x86 replacement with a caller-owned stack sentinel, checked the
  false result and exact stack restoration. All native installation/rollback
  suites passed for both address profiles and normal/laboratory builds.
- Ubisoft laboratory B reached its real main menu twice with the existing
  `Startup.tag`, without answering a reset question. Its `Options.ini` was
  byte-identical across the second start.
- Steam laboratory D reached its real main menu without a reset question.
  Steam was running. This test does not establish a client-free Steam start.
- All three game processes exited normally; original Ubisoft and Steam EXE
  hashes remain unchanged. No match or simulation test was needed for this
  startup-only change.
- The 31 installer tests passed with the new pinned adapter identities.

Sanitized results: [verification record](verification/startup-options-20261103.json).
