"""Explicit recipient-generated Leaf PS3 profile; no runtime activation."""
import hashlib,json,mmap,struct
from pathlib import Path
from . import inventory,leaf_wrapped,recipes,shaderlib,staging,staging_v2
CONFIG_BYTES=7204
CONFIG_VERSION=3
WORKER_BUDGET=8192
PUBLIC_MODULES=staging_v2.PUBLIC_MODULES_V2+('leaf_wrapped.py','staging_leaf_wrapped.py')
PROGRAMS={'leaf':('leaf-wrapped-diffuse-v4','pixel',1372,'284d5c84ee6700039ee818fe7f063b2b90536beac45e261a5b3e0cf8a6d1746a')}

def prepare(recipient_root,selection,display,*,config_version,available_config_versions,worker_overhead_bytes,
            available_features,available_scales,native_report,verified_platform=None,private_case=False):
    settings=staging._settings();selected=settings.validate(selection);settings.require_features(selected,available_features)
    if not selected['improved_vegetation']or selected['improved_terrain']or selected['improved_water']:
        raise ValueError('Explicit isolated Leaf wrapped PS selection required; no Hemi/Terrain/River stack')
    if type(config_version)is not int or config_version!=3:raise ValueError('Explicit material config version3 required')
    if(not isinstance(available_config_versions,(tuple,list,set,frozenset))
        or any(type(v)is not int or v not in(1,2,3)for v in available_config_versions)or 3 not in available_config_versions):raise ValueError('native_material_config_v3_unavailable')
    if type(worker_overhead_bytes)is not int or not 0<worker_overhead_bytes<=988:raise ValueError('V3 complete worker payload exceeds8192')
    if(not isinstance(available_scales,(tuple,list,set,frozenset))or any(type(v)is not int or v not in settings.SCALES for v in available_scales)
        or selected['render_scale']not in available_scales):raise ValueError('native_graphics_unavailable')
    if type(private_case)is not bool:raise ValueError('Invalid private case mode')
    dimensions=settings.render_size(selected,display);edition,archive_name=staging._layout(verified_platform)
    root=staging.plain(recipient_root);source=staging.plain(root/archive_name)
    if not root.is_dir()or not source.is_relative_to(root)or not source.is_file():raise ValueError('Recipient original graphics archive is absent')
    if not Path(native_report).is_absolute():raise ValueError('V3 report must be absolute')
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
            item=leaf_wrapped.generate(name,inventory.read_payload(archive,name));recipe,stage,size,digest=PROGRAMS['leaf']
            if(not isinstance(item,recipes.Generated)or len(item.program)!=size or shaderlib.digest(item.program)!=digest
                or item.manifest.get('generated_pixel_sha256')!=digest or item.manifest.get('active')is not False
                or item.manifest.get('source_resource')!=name or item.manifest.get('source_resource_sha256')!=expected
                or(item.manifest.get('original_vertex_sha256'),item.manifest.get('original_pixel_sha256'))!=(recipes.ORIGINAL_VERTEX,recipes.ORIGINAL_PIXEL)):
                raise ValueError('Generated Leaf PS identity mismatch')
            generated.append(item)
        again=inventory.bba.Archive(raw,copy=False)
        if hashlib.sha256(again.header+again.directory).hexdigest()!=directory:raise ValueError('Source directory changed')
    plans=[staging_v2._plan(cache_root,report.parent,'leaf',item,programs=PROGRAMS)for item in generated]
    if not plans:raise ValueError('No original vendor aliases selected')
    program=plans[0]['program']
    config=struct.pack('<5I4f',7204,3,4,dimensions['render_width'],dimensions['render_height'],1.15,.94,1.20,1.10)
    config+=bytes(3072)+staging_v2._path(str(effects_report),'.private.json')+bytes(2048)+staging_v2._path(str(program),'.ps.bin')
    if len(config)!=7204 or len(config)+worker_overhead_bytes>8192:raise AssertionError('V3 transport shape/budget mismatch')
    pending={};receipts=[]
    for plan in plans:
        for path,raw in((plan['program'],plan['bytes']),(plan['receipt'],plan['receipt_bytes'])):
            if path in pending and pending[path]!=raw:raise ValueError('Conflicting vendor aliases')
            pending[path]=raw
        receipts.append(plan['artifact']['receipt_relative_path'])
    for path,raw in pending.items():staging_v2._existing(path,raw)
    report.parent.mkdir(parents=True,exist_ok=True)
    for path,raw in pending.items():
        path.parent.mkdir(parents=True,exist_ok=True);staging.plain(path)
        if path.exists():staging_v2._existing(path,raw)
        else:
            with path.open('xb')as stream:stream.write(raw)
    artifact={**plans[0]['artifact'],'source_receipt_paths':receipts}
    return {'format':'s7-recipient-material-stage-v3','config_version':3,'config':config,'material_flags':4,'leaf_recipe_variant':4,
        'worker_overhead_bytes':worker_overhead_bytes,'worker_payload_bytes':len(config)+worker_overhead_bytes,
        'render_scale':selected['render_scale'],'dimensions':dimensions,'artifact_root':str(cache_root),'artifacts':{'leaf':artifact},
        'source_receipts':[{k:item.manifest[k]for k in('source_resource','source_resource_sha256','original_vertex_sha256','original_pixel_sha256','recipe')}for item in generated],
        'source_directory_sha256':directory,'full_archive_fingerprint_verified':False,'platform_edition':edition,'source_archive_relative_path':archive_name,
        'all_vendor_aliases_checked':True,'required_shader_quality':'high','original_vertex_shader_preserved':True,'no_Hemi_stack':True,
        'active':False,'runtime_qualified':False,'numeric_policy_qualified':False,'production_runtime_enabled':False,'ocean_enabled':False,'water_scope':None,
        'all_sources_generated_before_cache_writes':True,'all_paths_and_existing_cache_checked_before_writes':True}
