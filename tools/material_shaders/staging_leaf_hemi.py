"""Explicit isolated Leaf hemisphere recipe3 on unchanged wire2/6180 bytes.

Historical V2/Macro recipes remain immutable. All owned resource aliases and
all cache paths are preflighted before any write. No runtime is activated here.
"""
from . import leaf_hemi,staging,staging_v2
PUBLIC_MODULES=staging_v2.PUBLIC_MODULES_V2+("leaf_hemi.py","staging_leaf_hemi.py")
PROGRAMS={**staging_v2.PROGRAMS,
    "leaf":("leaf-hemisphere-contrast-v3","vertex",3852,"ffb6d89661e9ae8a9d75ba3d322a318996d07d1dbb81281fefdb4b8df2f277f7")}

def prepare(recipient_root,selection,display,*,config_version,available_config_versions,worker_overhead_bytes,
            available_features,available_scales,native_report,verified_platform=None,private_case=False):
    selected=staging._settings().validate(selection)
    if not selected['improved_vegetation']or selected['improved_terrain']or selected['improved_water']:
        raise ValueError('Explicit isolated Leaf hemisphere selection required; no Terrain or River mixing')
    result=staging_v2._prepare(recipient_root,selected,display,config_version=config_version,
        available_config_versions=available_config_versions,worker_overhead_bytes=worker_overhead_bytes,
        available_features=available_features,available_scales=available_scales,native_report=native_report,
        verified_platform=verified_platform,private_case=private_case,programs=PROGRAMS,leaf_vertex_generator=leaf_hemi.generate)
    result['leaf_recipe_variant']=3
    return result
