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
or launch and preservation of the non-Maximum SMAA selection.

The first actual DXVK comparison disabled SMAA while retaining Maximum; the
reported duplicate labels were not clearly reproduced in the observed static
views. The second comparison retained SMAA and disabled Maximum. Its actual
profile and shader selection were verified, but the window-capture helper
returned another foreground application, so those images are excluded from Gold
visual evidence. Both pairs exited normally and restored their profiles and
shared state.

A subsequent actual DXVK comparison displayed Gold in the foreground with SMAA,
tone and contacts active and Maximum disabled. Send and Start appeared clean in
the observed lobby; the user also confirmed that this view looked correct.
This establishes the selected profile as the verified local mitigation for the
reported UI artifact. It preserves the Modern renderer and SMAA.
Both comparison clients exited normally; their profiles, adapters, archives and
shared state were restored and verified afterward.

The exact shader or interaction responsible remains unconfirmed. Do not describe
SMAA alone as the established cause, or extend this menu observation to every
world scene, resolution or GPU.
