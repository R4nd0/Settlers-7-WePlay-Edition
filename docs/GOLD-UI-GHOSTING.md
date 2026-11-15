# Gold UI ghosting: routine test profile guard

A user reported displaced duplicate text on Gold's Send and Start buttons.
The private crossplay test profile still enabled the experimental Maximum
effects alongside SMAA. The ordinary Gold launcher already rejects Maximum.

`tools/gold/visual_runtime.py` now applies the same restriction to preparation,
verification and launching of routine Gold diagnostic profiles. An explicit
`allow_unqualified_maximum=True` argument is required to reproduce that
experimental path. This opt-in is a laboratory API, not a launcher choice.

The routine local test selection retains Modern/DXVK, SMAA, tone and contact
shading. Native quality, model detail and draw-distance settings are unchanged.
The earlier diagnostic selection is retained in a private backup.

Ten Gold graphics checks pass, including rejection before filesystem preparation
or launch and preservation of the non-Maximum SMAA selection. These source checks
establish the guard, not a visual cure.

The first actual DXVK comparison disabled SMAA while retaining Maximum; the
reported duplicate labels were not clearly reproduced in the observed static
views. The second comparison retained SMAA and disabled Maximum. Its actual
profile and shader selection were verified, but the window-capture helper
returned another foreground application, so those images are excluded from Gold
visual evidence. Both pairs exited normally and restored their profiles and
shared state.

The exact shader or interaction responsible for the reported artifact remains
unconfirmed. Do not describe SMAA as the established cause, or this profile
restriction as a fully verified rendering fix. A visible comparison is still
needed before making that claim.
