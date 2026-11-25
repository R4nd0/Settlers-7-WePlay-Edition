"""Explicit terrain recipe v3 on unchanged 6180-byte/version2 transport.

Only recipient-owned pinned sources generate programs. Historical V2 paths and
receipts stay immutable. This is a candidate, not release qualification.
"""
from . import staging,staging_v2,terrain_macro

VARIANT="terrain-macro-relief-v3"
PUBLIC_MODULES=staging_v2.PUBLIC_MODULES_V2+("terrain_macro.py","staging_macro.py")
PROGRAMS={**staging_v2.PROGRAMS,
    "bump_vertex":("terrain-macro-relief-BumpLayerShader-v3","vertex",2020,"da8154477a4fd3c44ace75d9c42d6d3714dc196c9c0f30fc332f06493539a21b"),
    "parallax_vertex":("terrain-macro-relief-ParallaxLayerShader-v3","vertex",2020,"b421d294516b89d0e0de90af56982b76ad8d196808c2d548ed251a3312313c7d"),
}

def prepare(recipient_root,selection,display,*,config_version,available_config_versions,worker_overhead_bytes,
            available_features,available_scales,native_report,verified_platform=None,private_case=False):
    settings=staging._settings();selected=settings.validate(selection)
    if not selected["improved_terrain"] or selected["improved_water"]:
        raise ValueError("Explicit terrain-macro-relief-v3 terrain selection without legacy water is required")
    return staging_v2._prepare(recipient_root,selected,display,config_version=config_version,
        available_config_versions=available_config_versions,worker_overhead_bytes=worker_overhead_bytes,
        available_features=available_features,available_scales=available_scales,native_report=native_report,
        verified_platform=verified_platform,private_case=private_case,programs=PROGRAMS,
        terrain_vertex_generator=terrain_macro.generate,terrain_scales=(1.0,1.0),recipe_variant=3)
