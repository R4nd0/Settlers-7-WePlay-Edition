"""User-approved graphics defaults for the release builder, not payload admission.

Apply only after native_graphics_runtime's existing qualification checks.
Optical acceptance does not populate QUALIFIED_BUILDS or replace package tests.
"""
from copy import deepcopy

PRESET_ID='clear-bank-leaf-b-20261126'
OPTICALLY_ACCEPTED=True
SETTINGS=dict(render_scale=200,improved_water=True,improved_terrain=False,
              improved_vegetation=True,stable_shadows=False)
PROGRAMS={
    'water':dict(name='Klareres Ufer',palette_index=1,recipe='river-palette-clear-bank-v3',
                 sha256='0bf632f2862eb635c25131e575b4c2d5c35ba9fa73cb3aa01591f9afaecd543c',bytes=3840),
    'vegetation':dict(name='Blatt B',palette_index=1,recipe='leaf-palette-B-v6',
                      sha256='284d5c84ee6700039ee818fe7f063b2b90536beac45e261a5b3e0cf8a6d1746a',bytes=1372),
}
DISABLED_EXPERIMENTS=('terrain_macro','shadow_filter','ambient_occlusion')


def defaults():
    """Requested release settings; rendering limits and capability gates still apply."""
    return deepcopy(SETTINGS)


def programs():
    return deepcopy(PROGRAMS)
