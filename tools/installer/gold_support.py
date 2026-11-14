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


def module(app):
    path=content.plain(Path(app)/'Tools/gold/installer.py')
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


def bundle(source,target,*,source_root=None):
    source,target=map(content.plain,(source,target))
    value=verify_package(source)
    if target.exists():raise ValueError('Gold payload destination exists')
    target.mkdir(parents=True)
    for name in ('package.json',*value['files']):
        path=content.plain(target/name);path.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(content.plain(source/name),path)
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
        return module(app).prepare_copy(app,source,target,**options)
    finally:sys.path[:]=previous


def verify(package,root):
    app=content.plain(Path(package)/FOLDER)
    value=module(app).verify_root(root)
    if value.get('platform_independent') is not True:raise ValueError('platform_isolation_required')
    return {'files':{**value['files'],**{name:content.digest(Path(root)/name) for name in ('gold-install.json','gold-development.json','README.txt')}}}
