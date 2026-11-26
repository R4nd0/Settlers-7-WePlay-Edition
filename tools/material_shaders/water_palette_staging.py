"""Explicit all-source-first recipient WaterPalette preparation; no activation."""
from pathlib import Path
import hashlib,json,mmap,struct
from . import inventory,recipes,staging,staging_v2,water,water_staging,water_palette as palette

def configuration(render,program_paths,*,enabled=False,selected_variant=0):
    render=water_staging._render(render)
    if type(enabled)is not bool or type(selected_variant)is not int or selected_variant not in(0,1,2):raise ValueError('Fixed Boolean/index required')
    if len(program_paths)!=3:raise ValueError('All3 fixed program paths required')
    raw=struct.pack('<6I',3096,2,*render,int(enabled),selected_variant)
    for i,path in enumerate(program_paths):
        path=staging.plain(path)
        if path.name!=palette.PINS[i]+'.ps.bin'or path.parent.name!=palette.POLICIES[i]['recipe']or path.parent.parent.name!='MaterialShaders':raise ValueError('Fixed ordered WaterPalette path mismatch')
        raw+=staging_v2._path(str(path),'.ps.bin')
    if len(raw)!=3096:raise AssertionError('Palette wire changed')
    return raw

def prepare(recipient_root,render,*,native_report,verified_platform,available_features,available_config_versions,
            worker_overhead_bytes,private_case=False,selected_variant=0):
    render=water_staging._render(render)
    if type(selected_variant)is not int or selected_variant not in(0,1,2):raise ValueError('Fixed palette selection required')
    if(not isinstance(available_config_versions,(tuple,list,set,frozenset))or any(type(v)is not int or v not in(1,2)for v in available_config_versions)or 2 not in available_config_versions):raise ValueError('native_water_palette_v2_unavailable')
    if type(worker_overhead_bytes)is not int or not 0<worker_overhead_bytes<=8192-3096:raise ValueError('Complete worker exceeds8192')
    if not isinstance(available_features,(tuple,list,set,frozenset))or 'improved_water'not in available_features:raise ValueError('native_graphics_unavailable')
    if type(private_case)is not bool:raise ValueError('Explicit private mode required')
    edition,archive_name=staging._layout(verified_platform);root=staging.plain(recipient_root);report=staging.plain(native_report)
    if not root.is_dir()or not report.parent.is_dir()or not report.name.endswith('.private.json'):raise ValueError('Owned existing root/private report required')
    staging_v2._path(str(report),'.private.json')
    if report.exists()and not report.is_file():raise ValueError('Report target must be regular or absent')
    if private_case:
        work=staging.plain(inventory.ROOT/'work')
        if(report.parent==work or not report.parent.is_relative_to(work)or report.parent.is_relative_to(root)
           or report.exists()or(report.parent/'checkpoint.private.json').exists()or(report.parent/'result.private.json').exists()):raise ValueError('Fresh separate private case required before checkpoint')
        artifact_root=report.parent;local=staging.plain(artifact_root/'LocalData')
    else:
        artifact_root=root;local=staging.plain(root/'LocalData')
        if report.parent!=local:raise ValueError('Recipient report must stay in LocalData')
    source=staging.plain(root/archive_name)
    if not source.is_relative_to(root)or not source.is_file():raise ValueError('Exact owned original graphics archive absent')
    generated=[]
    with source.open('rb')as stream,mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ)as raw:
        if len(raw)<32 or raw[:4]!=b'BAF\x06':raise ValueError('Unsupported original archive')
        header_size=struct.unpack_from('<I',raw,8)[0]
        if header_size not in(16,64)or len(raw)<16+header_size:raise ValueError('Invalid archive header')
        header=inventory.bba.crypt(raw[16:16+header_size],inventory.bba.HEAD_KEYS,1231853475,7)
        if struct.unpack_from('<I',header,8)[0]>32*1024*1024:raise ValueError('Source directory exceeds bound')
        archive=inventory.bba.Archive(raw,copy=False);directory=hashlib.sha256(archive.header+archive.directory).hexdigest()
        for name,source_hash in water.RESOURCES.items():
            resource=inventory.read_payload(archive,name)
            for i in range(3):
                item=palette.generate(name,resource,i)
                if(not isinstance(item,recipes.Generated)or len(item.program)!=3840 or hashlib.sha256(item.program).hexdigest()!=palette.PINS[i]
                   or item.manifest.get('source_resource')!=name or item.manifest.get('generated_pixel_sha256')!=palette.PINS[i]
                   or item.manifest.get('source_resource_sha256')!=source_hash or item.manifest.get('recipe')!=palette.POLICIES[i]['recipe']
                   or item.manifest.get('original_vertex_sha256')!=water.ORIGINAL_VERTEX or item.manifest.get('original_pixel_sha256')!=water.ORIGINAL_PIXEL
                   or item.manifest.get('public_binary_distribution_permitted')is not False or item.manifest.get('active')is not False):raise ValueError('Generated fixed WaterPalette identity mismatch')
                generated.append((i,item))
        again=inventory.bba.Archive(raw,copy=False)
        if hashlib.sha256(again.header+again.directory).hexdigest()!=directory:raise ValueError('Source directory changed')
    paths=[staging.plain(local/'MaterialShaders'/p['recipe']/(palette.PINS[i]+'.ps.bin'))for i,p in enumerate(palette.POLICIES)]
    config=configuration(render,paths,enabled=True,selected_variant=selected_variant)
    files={}
    for i,item in generated:
        path=paths[i]
        if path in files and files[path]!=item.program:raise ValueError('Vendor programs disagree')
        files[path]=item.program
        # Separate receipt preserves historical accepted-B metadata immutably.
        receipt=staging.plain(path.parent/(palette.PINS[i]+'-'+item.manifest['source_resource_sha256']+'-palette-v2.json'))
        files[receipt]=(json.dumps(item.manifest,sort_keys=True,indent=2)+'\n').encode()
    for path,data in files.items():water_staging._existing(path,data)
    for path,data in files.items():
        path.parent.mkdir(parents=True,exist_ok=True);staging.plain(path)
        if not water_staging._existing(path,data):
            with path.open('xb')as stream:stream.write(data)
    return dict(format='s7-recipient-water-palette-stage-v2',config_version=2,config_bytes=3096,config=config,
        selected_variant=selected_variant,artifact_root=str(artifact_root),edition=edition,render=list(render),private_case=private_case,
        worker_overhead_bytes=worker_overhead_bytes,worker_payload_bytes=3096+worker_overhead_bytes,
        palette_variants=[dict(index=i,name=p['name'],recipe=p['recipe'],sha256=palette.PINS[i],bytes=3840,
            program_path=str(paths[i]),program_relative_path=paths[i].relative_to(artifact_root).as_posix())for i,p in enumerate(palette.POLICIES)],
        source_resources=dict(water.RESOURCES),source_receipts=[item.manifest for _,item in generated],source_directory_sha256=directory,
        all_vendor_aliases_checked=True,all_sources_generated_before_cache_writes=True,full_archive_fingerprint_verified=False,
        accepted_B_preserved=True,required_shader_quality='high',original_vertex_shader_preserved=True,
        active=False,runtime_verified=False,visible_quality_qualified=False,no_legacy_river_scaling_permitted=True)
