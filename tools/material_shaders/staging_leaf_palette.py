"""Explicit recipient-generated Leaf PS palette; no runtime activation."""
import hashlib,json,mmap,struct
from pathlib import Path
from . import inventory,leaf_palette,recipes,shaderlib,staging,staging_v2
CONFIG_BYTES=4120
CONFIG_VERSION=4
WORKER_BUDGET=8192
PUBLIC_MODULES=staging_v2.PUBLIC_MODULES_V2+('leaf_wrapped.py','leaf_palette.py','staging_leaf_palette.py')
PROGRAMS={
    'leaf_A':('leaf-palette-A-v5','pixel',1372,'1b825358f46c39b04b1d540738f4be5e862bfe6627b8545ab092c471d259477c'),
    'leaf_B':('leaf-palette-B-v6','pixel',1372,'284d5c84ee6700039ee818fe7f063b2b90536beac45e261a5b3e0cf8a6d1746a'),
    'leaf_C':('leaf-palette-C-v7','pixel',1372,'b869fe375b76509f60a48d34bde83565d28c0416aca85f27c8a445045cb6a51f')}


def prepare(recipient_root,selection,display,*,config_version,available_config_versions,worker_overhead_bytes,
            available_features,available_scales,native_report,verified_platform=None,private_case=False,selected_variant=0):
    settings=staging._settings();selected=settings.validate(selection);settings.require_features(selected,available_features)
    if not selected['improved_vegetation']or selected['improved_terrain']or selected['improved_water']:
        raise ValueError('Explicit isolated Leaf wrapped PS selection required; no Hemi/Terrain/River stack')
    if type(selected_variant)is not int or selected_variant not in(0,1,2):raise ValueError('Fixed palette selection0..2 required')
    if type(config_version)is not int or config_version!=4:raise ValueError('Explicit material config version4 required')
    if(not isinstance(available_config_versions,(tuple,list,set,frozenset))
        or any(type(v)is not int or v not in(1,2,3,4)for v in available_config_versions)or 4 not in available_config_versions):raise ValueError('native_material_config_v4_unavailable')
    if type(worker_overhead_bytes)is not int or not 0<worker_overhead_bytes<=4072:raise ValueError('V4 complete worker payload exceeds8192')
    if(not isinstance(available_scales,(tuple,list,set,frozenset))or any(type(v)is not int or v not in settings.SCALES for v in available_scales)
        or selected['render_scale']not in available_scales):raise ValueError('native_graphics_unavailable')
    if type(private_case)is not bool:raise ValueError('Invalid private case mode')
    dimensions=settings.render_size(selected,display);edition,archive_name=staging._layout(verified_platform)
    root=staging.plain(recipient_root);source=staging.plain(root/archive_name)
    if not root.is_dir()or not source.is_relative_to(root)or not source.is_file():raise ValueError('Recipient original graphics archive is absent')
    if not Path(native_report).is_absolute():raise ValueError('V4 report must be absolute')
    report=staging.plain(native_report);local=staging.plain(root/'LocalData')
    if report.suffix.lower()!='.json':raise ValueError('Invalid native report location')
    if private_case:
        work=staging.plain(inventory.ROOT/'work')
        if(report.parent==work or not report.parent.is_relative_to(work)or not report.parent.is_dir()
            or report.parent.is_relative_to(root/Path(archive_name).parts[0])):raise ValueError('Private output must stay in a separate work case')
        cache_root=report.parent
    else:
        if report.parent!=local:raise ValueError('Production report must be directly under recipient LocalData')
        cache_root=root
    effects_report=report.parent/'material-effects.private.json'
    if effects_report==report:raise ValueError('Native/effects report collision')
    staging_v2._path(str(report),'.json');staging_v2._path(str(effects_report),'.private.json')
    for path in(report,effects_report):
        if path.exists()and not path.is_file():raise ValueError('Report target must be regular or absent')
    generated=[]
    with source.open('rb')as stream,mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ)as raw:
        if len(raw)<32 or raw[:4]!=b'BAF\x06':raise ValueError('Unsupported original archive')
        header_size=struct.unpack_from('<I',raw,8)[0]
        if header_size not in(16,64)or len(raw)<16+header_size:raise ValueError('Invalid archive header')
        header=inventory.bba.crypt(raw[16:16+header_size],inventory.bba.HEAD_KEYS,1231853475,7)
        if struct.unpack_from('<I',header,8)[0]>32*1024*1024:raise ValueError('Source directory exceeds bound')
        archive=inventory.bba.Archive(raw,copy=False);directory=hashlib.sha256(archive.header+archive.directory).hexdigest()
        for name,expected in recipes.RESOURCES.items():
            data=inventory.read_payload(archive,name)
            for index,variant in enumerate(('A','B','C')):
                role='leaf_'+variant;item=leaf_palette.generate(name,data,index);recipe,stage,size,digest=PROGRAMS[role]
                if(not isinstance(item,recipes.Generated)or len(item.program)!=size or shaderlib.digest(item.program)!=digest
                    or item.manifest.get('generated_pixel_sha256')!=digest or item.manifest.get('active')is not False
                    or item.manifest.get('source_resource')!=name or item.manifest.get('source_resource_sha256')!=expected
                    or(item.manifest.get('original_vertex_sha256'),item.manifest.get('original_pixel_sha256'))!=(recipes.ORIGINAL_VERTEX,recipes.ORIGINAL_PIXEL)):
                    raise ValueError('Generated palette PS identity mismatch')
                generated.append((role,item))
        again=inventory.bba.Archive(raw,copy=False)
        if hashlib.sha256(again.header+again.directory).hexdigest()!=directory:raise ValueError('Source directory changed')
    plans=[staging_v2._plan(cache_root,report.parent,role,item,programs=PROGRAMS)for role,item in generated]
    paths={plan['role']:plan['program']for plan in plans}
    if set(paths)!={'leaf_A','leaf_B','leaf_C'}:raise ValueError('All three fixed palette programs required')
    config=struct.pack('<6I',4120,4,4,dimensions['render_width'],dimensions['render_height'],selected_variant)
    for role in('leaf_A','leaf_B','leaf_C'):config+=staging_v2._path(str(paths[role]),'.ps.bin')
    config+=staging_v2._path(str(effects_report),'.private.json')
    if len(config)!=4120 or len(config)+worker_overhead_bytes>8192:raise AssertionError('V4 shape/budget mismatch')
    pending={};artifacts={}
    for plan in plans:
        for path,raw in((plan['program'],plan['bytes']),(plan['receipt'],plan['receipt_bytes'])):
            if path in pending and pending[path]!=raw:raise ValueError('Conflicting vendor aliases')
            pending[path]=raw
        role=plan['role']
        if role not in artifacts:artifacts[role]={**plan['artifact'],'source_receipt_paths':[]}
        artifacts[role]['source_receipt_paths'].append(plan['artifact']['receipt_relative_path'])
    for path,raw in pending.items():staging_v2._existing(path,raw)
    report.parent.mkdir(parents=True,exist_ok=True)
    for path,raw in pending.items():
        path.parent.mkdir(parents=True,exist_ok=True);staging.plain(path)
        if path.exists():staging_v2._existing(path,raw)
        else:
            with path.open('xb')as stream:stream.write(raw)
    palette=[dict(index=i,name=name,recipe=PROGRAMS['leaf_'+name][0],sha256=PROGRAMS['leaf_'+name][3],bytes=1372)for i,name in enumerate(('A','B','C'))]
    return {'format':'s7-recipient-material-stage-v4','config_version':4,'config':config,'material_flags':4,'leaf_recipe_variant':5+selected_variant,
        'selected_variant':selected_variant,'palette_variants':palette,
        'worker_overhead_bytes':worker_overhead_bytes,'worker_payload_bytes':len(config)+worker_overhead_bytes,
        'render_scale':selected['render_scale'],'dimensions':dimensions,'artifact_root':str(cache_root),'artifacts':artifacts,
        'source_receipts':[{k:item.manifest[k]for k in('source_resource','source_resource_sha256','original_vertex_sha256','original_pixel_sha256','recipe')}for _,item in generated],
        'source_directory_sha256':directory,'full_archive_fingerprint_verified':False,'platform_edition':edition,'source_archive_relative_path':archive_name,
        'all_vendor_aliases_checked':True,'required_shader_quality':'high','original_vertex_shader_preserved':True,'no_Hemi_stack':True,
        'active':False,'runtime_qualified':False,'numeric_policy_qualified':False,'production_runtime_enabled':False,'ocean_enabled':False,'water_scope':None,
        'all_sources_generated_before_cache_writes':True,'all_paths_and_existing_cache_checked_before_writes':True}
