"""Compact V5 setup of the unchanged Leaf palette and Terrain macro programs."""
import hashlib,json,mmap,struct
from pathlib import Path
from . import inventory,leaf_palette,recipes,shaderlib,staging,staging_v2,staging_leaf_palette,staging_macro,terrain_macro

CONFIG_BYTES=24
CONFIG_VERSION=5
PUBLIC_MODULES=tuple(dict.fromkeys(staging_leaf_palette.PUBLIC_MODULES+staging_macro.PUBLIC_MODULES+('staging_combined.py',)))
PROGRAMS={**staging_leaf_palette.PROGRAMS,**{k:v for k,v in staging_macro.PROGRAMS.items()if k!='leaf'}}
SLOT_ORDER=('leaf_A','bump_pixel','parallax_pixel','bump_vertex','parallax_vertex','leaf_B','leaf_C')

def prepare(recipient_root,selection,display,*,config_version,available_config_versions,worker_overhead_bytes,
            available_features,available_scales,native_report,verified_platform=None,private_case=False,
            enabled_flags=0,selected_variant=0):
    settings=staging._settings();selected=settings.validate(selection);settings.require_features(selected,available_features)
    if not selected['improved_terrain']or not selected['improved_vegetation']or selected['improved_water']:
        raise ValueError('V5 requires both Terrain/Leaf; WaterProgram stays separate from legacy River')
    if type(config_version)is not int or config_version!=5:raise ValueError('Explicit material config version5 required')
    if(not isinstance(available_config_versions,(tuple,list,set,frozenset))or any(type(v)is not int or v not in(1,2,3,4,5)for v in available_config_versions)or 5 not in available_config_versions):raise ValueError('native_material_config_v5_unavailable')
    if type(worker_overhead_bytes)is not int or not 0<worker_overhead_bytes<=8192-24:raise ValueError('Complete V5 worker payload exceeds8192')
    if type(enabled_flags)is not int or enabled_flags not in(0,2,4,6)or type(selected_variant)is not int or selected_variant not in(0,1,2):raise ValueError('Fixed V5 flags/index required')
    if(not isinstance(available_scales,(tuple,list,set,frozenset))or any(type(v)is not int or v not in settings.SCALES for v in available_scales)or selected['render_scale']not in available_scales):raise ValueError('native_graphics_unavailable')
    if type(private_case)is not bool:raise ValueError('Explicit private case mode required')
    dimensions=settings.render_size(selected,display);edition,archive_name=staging._layout(verified_platform)
    root=staging.plain(recipient_root);source=staging.plain(root/archive_name)
    if not root.is_dir()or not source.is_relative_to(root)or not source.is_file():raise ValueError('Owned original graphics archive absent')
    if not Path(native_report).is_absolute():raise ValueError('Absolute trusted native report required')
    report=staging.plain(native_report);local=staging.plain(root/'LocalData')
    if private_case:
        work=staging.plain(inventory.ROOT/'work')
        if(report.parent==work or not report.parent.is_relative_to(work)or not report.parent.is_dir()or report.parent.is_relative_to(root/Path(archive_name).parts[0])
            or(report.parent/'checkpoint.private.json').exists()or(report.parent/'result.private.json').exists()):raise ValueError('Separate unlaunched private work case required')
        cache_root=report.parent
    else:
        if report.parent!=local:raise ValueError('Recipient report must stay directly in LocalData')
        cache_root=root
    effects_report=report.parent/'material-effects.private.json'
    if effects_report==report:raise ValueError('Native/material report collision')
    staging_v2._path(str(report),'.json');staging_v2._path(str(effects_report),'.private.json')
    for path in(report,effects_report):
        if path.exists()and not path.is_file():raise ValueError('Report target must be regular or absent')
    generated=[]
    with source.open('rb')as stream,mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ)as raw:
        if len(raw)<32 or raw[:4]!=b'BAF\x06':raise ValueError('Unsupported owned original archive')
        header_size=struct.unpack_from('<I',raw,8)[0]
        if header_size not in(16,64)or len(raw)<16+header_size:raise ValueError('Invalid archive header')
        header=inventory.bba.crypt(raw[16:16+header_size],inventory.bba.HEAD_KEYS,1231853475,7)
        if struct.unpack_from('<I',header,8)[0]>32*1024*1024:raise ValueError('Source directory exceeds bound')
        archive=inventory.bba.Archive(raw,copy=False);directory=hashlib.sha256(archive.header+archive.directory).hexdigest()
        for name,source_pin in recipes.RESOURCES.items():
            resource=inventory.read_payload(archive,name)
            for i,label in enumerate('ABC'):
                role='leaf_'+label;item=leaf_palette.generate(name,resource,i);recipe,stage,size,pin=PROGRAMS[role]
                if(not isinstance(item,recipes.Generated)or len(item.program)!=size or shaderlib.digest(item.program)!=pin
                    or item.manifest.get('generated_pixel_sha256')!=pin or item.manifest.get('source_resource')!=name
                    or item.manifest.get('source_resource_sha256')!=source_pin or item.manifest.get('recipe')!=recipe
                    or item.manifest.get('active')is not False or(item.manifest.get('original_vertex_sha256'),item.manifest.get('original_pixel_sha256'))!=(recipes.ORIGINAL_VERTEX,recipes.ORIGINAL_PIXEL)):
                    raise ValueError('V5 Leaf generated program identity mismatch')
                generated.append((role,item))
        for name in recipes.TERRAIN_RESOURCES:
            resource=inventory.read_payload(archive,name)
            for record,prefix in(('BumpLayerShader','bump'),('ParallaxLayerShader','parallax')):
                for stage,generator in(('vertex',terrain_macro.generate),('pixel',recipes.generate_terrain)):
                    role=prefix+'_'+stage;item=staging_v2._versioned(role,generator(name,resource,record),programs=PROGRAMS)
                    generated.append((role,item))
        again=inventory.bba.Archive(raw,copy=False)
        if hashlib.sha256(again.header+again.directory).hexdigest()!=directory:raise ValueError('Source directory changed')
    plans=[staging_v2._plan(cache_root,report.parent,role,item,programs=PROGRAMS)for role,item in generated]
    paths={p['role']:p['program']for p in plans}
    if set(paths)!=set(SLOT_ORDER):raise ValueError('All seven fixed combined programs required')
    config=struct.pack('<6I',24,5,enabled_flags,dimensions['render_width'],dimensions['render_height'],selected_variant)
    pending={};artifacts={}
    for plan in plans:
        for path,raw in((plan['program'],plan['bytes']),(plan['receipt'],plan['receipt_bytes'])):
            if path in pending and pending[path]!=raw:raise ValueError('Conflicting original vendor aliases')
            pending[path]=raw
        role=plan['role']
        if role not in artifacts:artifacts[role]={**plan['artifact'],'source_receipt_paths':[]}
        artifacts[role]['source_receipt_paths'].append(plan['artifact']['receipt_relative_path'])
    for path,raw in pending.items():staging_v2._existing(path,raw)
    for path,raw in pending.items():
        path.parent.mkdir(parents=True,exist_ok=True);staging.plain(path)
        if path.exists():staging_v2._existing(path,raw)
        else:
            with path.open('xb')as stream:stream.write(raw)
    return dict(format='s7-recipient-material-stage-v5',config_version=5,config=config,material_flags=6,initial_flags=enabled_flags,
        selected_variant=selected_variant,artifact_root=str(cache_root),artifacts=artifacts,
        program_slots=[dict(slot=i,role=role,recipe=PROGRAMS[role][0],stage=PROGRAMS[role][1],bytes=PROGRAMS[role][2],sha256=PROGRAMS[role][3],program_relative_path=artifacts[role]['program_relative_path'])for i,role in enumerate(SLOT_ORDER)],
        source_receipts=[item.manifest for _,item in generated],all_vendor_aliases_checked=True,
        source_directory_sha256=directory,full_archive_fingerprint_verified=False,platform_edition=edition,source_archive_relative_path=archive_name,
        worker_overhead_bytes=worker_overhead_bytes,worker_payload_bytes=24+worker_overhead_bytes,dimensions=dimensions,render_scale=selected['render_scale'],
        terrain_recipe_variant=3,leaf_recipe_variant=5+selected_variant,terrain_detail_scale=1.0,terrain_relief_scale=1.0,
        required_shader_quality='high',all_sources_generated_before_cache_writes=True,all_paths_and_existing_cache_checked_before_writes=True,
        active=False,runtime_qualified=False,visible_quality_qualified=False,production_runtime_enabled=False,no_legacy_river_scaling_permitted=True)
