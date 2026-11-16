"""Gold's separate native/runtime contract inside the common desktop package."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import compatibility as content
import platform_standalone

FOLDER = 'Payload/Gold'
EXE = 'Data/Base/_Dbg/Bin/Release/Settlers7R.exe'


def module(app, *, source_root=None):
    path=content.plain(Path(source_root)/'tools/gold/installer.py' if source_root is not None else Path(app)/'Tools/gold/installer.py')
    spec=importlib.util.spec_from_file_location('weplay_bundled_gold_installer',path)
    result=importlib.util.module_from_spec(spec)
    previous=sys.path[:]
    try:
        sys.path.insert(0,str(path.parent));spec.loader.exec_module(result)
        import bootstrap,profile_interface
    finally:sys.path[:]=previous
    return result


def verify_package(app):
    runtime=module(app)
    value=runtime.verify_package(content.plain(app))
    if value.get('platform_independent') is not True:raise ValueError('platform_isolation_required')
    return value


def verify_source_package(app, *, source_root):
    # A previous package is build input, never authority to ship an old adapter.
    # Validate it with the current trusted verifier, not its older Python code.
    runtime=module(app,source_root=source_root)
    value=runtime.verify_package(content.plain(app),source_package=True)
    if value.get('platform_independent') is not True:raise ValueError('platform_isolation_required')
    return value


def adapter_input(source, adapter=None):
    path=content.plain(adapter if adapter is not None else Path(source)/'Payload/weplay_gold_startup.dll')
    if content.digest(path)!=platform_standalone.GOLD:
        raise ValueError('Supply the current verified Gold production adapter; the source package has an older or unknown adapter.')
    return path


def bundle(source,target,*,source_root=None,quiet_reshade=None,adapter=None):
    source,target=map(content.plain,(source,target))
    value=verify_source_package(source,source_root=source_root) if source_root is not None else verify_package(source)
    if target.exists():raise ValueError('Gold payload destination exists')
    adapter=adapter_input(source,adapter)
    payload=None
    if quiet_reshade is not None:
        import reshade_runtime
        payload=content.plain(quiet_reshade)
        if content.digest(payload)!=reshade_runtime.SETUP_SHA256:raise ValueError('Quiet ReShade bundle fingerprint mismatch')
        existing=content.plain(source/reshade_runtime.RUNTIME_BUNDLE)
        if existing.exists() and content.digest(existing)!=reshade_runtime.SETUP_SHA256:
            raise ValueError('Existing Gold runtime bundle fingerprint mismatch')
    target.mkdir(parents=True)
    for name in ('package.json',*value['files']):
        path=content.plain(target/name);path.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(content.plain(source/name),path)
    shutil.copyfile(adapter,content.plain(target/'Payload/weplay_gold_startup.dll'))
    if source_root is not None:
        repo=content.plain(source_root)
        # Reuse pinned native/interpreter inputs, while always shipping the
        # current installer sources. A default rebuild must not freeze old Gold
        # repair code merely because its base package came from an earlier RC.
        for name in value['files']:
            if name.startswith('Tools/gold/'):
                local=repo/'tools'/('installer' if Path(name).name in (*platform_standalone.TOOLS,'compatibility.py') else 'gold')/Path(name).name
            elif name=='Tools/lan/bba.py':local=repo/'tools/lan/bba.py'
            else:continue
            shutil.copyfile(content.plain(local),content.plain(target/name))
        for name in ('release_interface.py','graphics_assets.py'):
            shutil.copyfile(content.plain(repo/'tools/gold'/name),content.plain(target/'Tools/gold'/name))
        shared=target/'Tools/midsummer';shared.mkdir(exist_ok=True)
        for name in ('profile.py','archive_append.py','camera_preferences.lua','runtime.lua'):
            shutil.copyfile(content.plain(repo/'tools/midsummer'/name),content.plain(shared/name))
        runtime=module(target)
        value['menu_sha256']=runtime.gold.MENU_SHA256
        value['files']=runtime.inventory(target,('Tools','Runtime','Payload','Definitions','Licenses'))
        (target/'package.json').write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    if payload is not None:
        destination=content.plain(target/reshade_runtime.RUNTIME_BUNDLE)
        destination.parent.mkdir(parents=True,exist_ok=True)
        if not destination.exists():shutil.copyfile(payload,destination)
        if source_root is not None:
            shutil.copyfile(content.plain(Path(source_root)/'tools/gold/RESHade-LICENSE.txt'),destination.with_name('ReShade-LICENSE.txt'))
        runtime=module(target)
        value['files']=runtime.inventory(target,('Tools','Runtime','Payload','Definitions','Licenses'))
        (target/'package.json').write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    verify_package(target)


def is_gold(root):
    root=content.plain(root)
    return ((root/'gold-install.json').exists() or (root/EXE).is_file()
            or (root/'Settlers 7'/EXE).is_file())


def inspect(package,source,*,managed=False):
    app=content.plain(Path(package)/FOLDER);verify_package(app);runtime=module(app)
    if managed:
        originals,_=runtime.repair_inputs(source)
        return runtime.gold.inspect(source,app/'Definitions/Definitions.bin',originals=originals)
    return runtime.gold.inspect(source,app/'Definitions/Definitions.bin')


def prepare(package,source,target,**options):
    app=content.plain(Path(package)/FOLDER);verify_package(app)
    previous=sys.path[:]
    try:
        sys.path.insert(0,str(app/'Tools/gold'))
        # Gold has its own preparation path, so History's Runtime copy does not
        # run here. Carry the same qualified effect runtime into the new copy.
        import reshade_runtime
        payload=content.plain(app/reshade_runtime.RUNTIME_BUNDLE)
        if payload.exists() and content.digest(payload)!=reshade_runtime.SETUP_SHA256:
            raise ValueError('Quiet ReShade bundle fingerprint mismatch')
        result=module(app).prepare_copy(app,source,target,**options)
        if payload.exists():
            destination=content.plain(Path(target)/reshade_runtime.RUNTIME_BUNDLE)
            destination.parent.mkdir(parents=True,exist_ok=True)
            with payload.open('rb') as incoming,destination.open('xb') as outgoing:
                shutil.copyfileobj(incoming,outgoing)
            if content.digest(destination)!=reshade_runtime.SETUP_SHA256:
                raise ValueError('Quiet ReShade copy fingerprint mismatch')
            if options.get('tracker'):options['tracker'](destination,reshade_runtime.SETUP_SHA256)
        return result
    finally:sys.path[:]=previous


def verify(package,root):
    app=content.plain(Path(package)/FOLDER)
    value=module(app).verify_root(root)
    if value.get('platform_independent') is not True:raise ValueError('platform_isolation_required')
    return {'files':{**value['files'],**{name:content.digest(Path(root)/name) for name in ('gold-install.json','gold-development.json','README.txt')}}}
