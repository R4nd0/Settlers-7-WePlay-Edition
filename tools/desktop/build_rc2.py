"""Build the default isolated Gold/Ubisoft/Steam desktop package from pinned inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from urllib.parse import quote

REPO=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(REPO/'tools/installer'))
import build_package
import compatibility as content
import install

RELEASE_GUIDE = REPO/'docs/RC3.25-STANDALONE-UPDATE.md'
RELEASE_TAG = 'v2.0.0-rc3.25'


def public_document(source,*,revision=None):
    """Packaged documents link to versioned sources, not absent local folders."""
    source = Path(source)
    if revision is None:revision=RELEASE_TAG
    elif not isinstance(revision,str) or not re.fullmatch('[0-9a-f]{40}',revision):
        raise ValueError('Use the exact source commit for new packaged documents')
    def link(match):
        path, separator, fragment = match[1].partition('#')
        target = (source.parent / path).resolve()
        if not target.is_relative_to(REPO.resolve()) or not target.is_file():
            raise ValueError('Missing public document target: ' + match[1])
        relative = target.relative_to(REPO.resolve()).as_posix()
        return '](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/' + revision + '/' + quote(relative) + (separator + fragment if separator else '') + ')'
    return re.sub(r'\]\((?!https?://|#)([^)]+)\)', link, source.read_text(encoding='utf-8'))


def copy_documents(app,*,platform_independent=False):
    guide=REPO/'docs/RC3.27-MEMORY-GOLD.md' if platform_independent else RELEASE_GUIDE
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip() if platform_independent else None
    for original, destination in ((guide,'LIESMICH.txt'),
            (guide,'RELEASE-NOTES.md'),(guide,'README.md'),
            (REPO/'CREDITS.md','CREDITS.md'),(REPO/'docs/DISCORD-INTEGRATION.md','DISCORD-INTEGRATION.md')):
        (app/destination).write_text(public_document(original,revision=revision),encoding='utf-8',newline='\n')
    shutil.copyfile(REPO/'LICENSE',app/'LICENSE')


def build(stage,output,desktop_publish,dotnet,probe,compiler,*,base_package=None,service=None,adapter=None,python_zip=None,dxvk_dll=None,dxvk_license=None,memory_scanner=None,easytier_runtime=None,easytier_source=None,windows_helpers=None,update_sequence=27,game_directory=False,neo_logistics=False,midsummer_profile=False,atmosphere_addon=None,platform_runtime=None,gold_package=None):
    stage,output,desktop_publish,dotnet,probe,compiler=map(content.plain,(stage,output,desktop_publish,dotnet,probe,compiler))
    if stage.exists() or not stage.parent.is_dir():raise ValueError('Use a new stage directory')
    if not stage.is_relative_to(REPO/'work'):raise ValueError('Stage must be inside work')
    if not output.is_relative_to(REPO/'outputs'):raise ValueError('Preview output must be inside outputs')
    if easytier_runtime is not None and easytier_source is None:raise ValueError('Corresponding EasyTier source is required')
    if type(update_sequence) is not int or not 1<=update_sequence<=2147483647:raise ValueError('Invalid update sequence')
    if not compiler.is_file() or not probe.is_file():raise ValueError('Build tools missing')
    if not (desktop_publish/'S7Launcher.exe').is_file():raise ValueError('Desktop app publish is missing')
    old=content.plain(base_package or REPO/'work/release-inputs/S7-AutoLAN-Testpaket-2026-10-28-r3')
    import platform_standalone,gold_support,release_policy
    # A missing input is a build error, never permission to ship a platform-linked
    # main package. Subsequent builds inherit both runtimes from the last package.
    platform_runtime=platform_standalone.payload(platform_runtime or old/'Payload/Platform')
    gold_package=content.plain(gold_package or old/'Payload/Gold')
    gold_support.verify_package(gold_package)
    stage.mkdir();output.mkdir(parents=True,exist_ok=True)
    app=stage/'App';app.mkdir()
    for path in desktop_publish.iterdir():
        content.plain(path)
        if path.is_file() and path.suffix in ('.exe','.dll','.json'):shutil.copyfile(path,app/path.name)
    version='10.0.15'
    runtime=app/'DesktopRuntime';runtime.mkdir()
    for name in ('dotnet.exe','LICENSE.txt','ThirdPartyNotices.txt'):shutil.copyfile(dotnet/name,runtime/name)
    for relative in ('host/fxr/'+version,'shared/Microsoft.NETCore.App/'+version,'shared/Microsoft.WindowsDesktop.App/'+version):
        shutil.copytree(dotnet/relative,runtime/relative)
    # The base supplies previously verified runtime/server inputs, not the new
    # feature contract. Its historical adapter may have a different valid pin.
    previous=json.loads((old/'package.json').read_text(encoding='utf-8'))
    if previous.get('format')!=install.FORMAT or content.inventory(old,('Payload','Runtime','Tools'))!=previous.get('files'):
        raise ValueError('Base package inventory does not match its manifest')
    build_package.build(adapter or old/'Payload/uplay_r1_loader.dll',service or old/'Payload/Server',dotnet,
        python_zip or REPO/'work/installer-runtime/python-3.14.10-embed-amd64.zip',app/'Package',
        dxvk_dll=dxvk_dll or REPO/'work/dxvk-3.1.1-x32/d3d9.dll',
        dxvk_license=dxvk_license or REPO/'work/dxvk-3.1.1-x32/LICENSE',graphics_probe=probe,
        memory_scanner=memory_scanner or old/'Tools/d3d11/memory_scan.dll',
        easytier_runtime=easytier_runtime,windows_helpers=windows_helpers,game_directory=game_directory,neo_logistics=neo_logistics,midsummer_profile=midsummer_profile,atmosphere_addon=atmosphere_addon,platform_runtime=platform_runtime)
    gold_support.bundle(gold_package,app/'Package'/gold_support.FOLDER,source_root=REPO)
    package_record=json.loads((app/'Package/package.json').read_text(encoding='utf-8'))
    package_record.update(main_package=True,gold_standalone=True,platform_isolation=release_policy.CONTRACT)
    package_record['files']=content.inventory(app/'Package',('Payload','Runtime','Tools'))
    (app/'Package/package.json').write_text(json.dumps(package_record,indent=2)+'\n',encoding='utf-8')
    install.package_manifest(app/'Package')
    # CMDs are private development entrypoints, never part of the new user flow.
    # They are not removed from old packages or installed game copies.
    for path in (app/'Package').glob('*.cmd'):path.unlink()
    for name in ('LIESMICH.txt','START-HIER.txt'):(app/'Package'/name).unlink(missing_ok=True)
    copy_documents(app,platform_independent=platform_runtime is not None)
    if easytier_runtime is not None:
        sys.path.insert(0,str(REPO/'tools/easytier'))
        import source_bundle
        sources=app/'Source';sources.mkdir()
        source_bundle.build(easytier_source,sources/'EasyTier-corresponding-source.zip')
        shutil.copyfile(sources/'EasyTier-corresponding-source.zip',output/'EasyTier-corresponding-source.zip')
    release={'format':'s7-desktop-release-v1','release':'RC3.27 Memory and Gold fixes','network_revision':9,'game_bits':32,
             'version':'2.0.0-rc3.27','update_sequence':update_sequence,'update_channel':'preview','midsummer_profile':midsummer_profile,
             'platform_isolation':release_policy.CONTRACT,'gold_standalone':True,'main_package':True,
             'neo_logistics':neo_logistics,'neo_wire_revision':5 if neo_logistics else None,'platform_independent':platform_runtime is not None,
             'internet_transport':'EasyTier 2.6.4-weplay-npcap-free-acl1' if easytier_runtime is not None else None,
             'internet_gameplay_verified':False,
             'project':'Settlers-7-WePlay-Edition',
             'source_repository':'https://github.com/R4nd0/Settlers-7-WePlay-Edition',
             'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip(),
             'source_tree_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=REPO,text=True).strip()),
             'package_id':content.fingerprint(install.package_manifest(app/'Package')['files']),
             'probe_sha256':content.digest(probe),'compiler':'Inno Setup 7.1.1 x64','authenticode_signed':False}
    (app/'desktop-release.json').write_text(json.dumps(release,indent=2)+'\n',encoding='utf-8')
    compile_setup(app,output,compiler)
    proof={'format':'weplay-main-package-release-v1','platform_isolation':release_policy.CONTRACT,
           'setup_sha256':content.digest(output/'Setup.exe'),'setup_bytes':(output/'Setup.exe').stat().st_size,
           'source_commit':release['source_commit'],'package_id':release['package_id']}
    (output/'Setup.release.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
    return app


def compile_setup(app,output,compiler):
    if any((app/name).exists() for name in ('State','Instances','LocalData','Game','Backup')):
        raise ValueError('Private machine/game data must never be included in Setup')
    result=subprocess.run([str(compiler),'/Qp','/DStage='+str(app),'/DOutput='+str(output),str(REPO/'tools/desktop/Setup.iss')],
                          cwd=REPO,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+'\n'+result.stderr)
    target=output/'Setup.exe'
    if not target.is_file():raise RuntimeError('Setup compiler produced no installer')
    target.with_suffix('.exe.sha256').write_text(content.digest(target)+'  Setup.exe\n',encoding='ascii')
    print(json.dumps({'setup':str(target),'bytes':target.stat().st_size,'sha256':content.digest(target)}))


def refresh_stage(app,desktop_publish):
    """Refresh an uninstalled private build stage; never a loaded game/installation."""
    app,desktop_publish=map(content.plain,(app,desktop_publish))
    if not app.is_relative_to(REPO/'work') or (app/'State/installation.json').exists():
        raise ValueError('Only an uninstalled private stage can be refreshed')
    package=app/'Package'
    manifest=install.package_manifest(package)
    license_sources={'tools/licenses/neo-logistics-puff.txt':REPO/'native/third_party/puff/puff.h',
                     'tools/licenses/neo-logistics-s7packer.txt':REPO/'tools/lan/LICENSE.S7Packer.txt'}
    for name in manifest['files']:
        if name=='tools/d3d11/memory_scan.dll':
            sys.path.insert(0,str(REPO/'tools/d3d11'))
            import memory_guard
            memory_guard.verify_scanner(package/name)
            continue
        if name.startswith('tools/') and name!='tools/license.txt':
            source=content.plain(license_sources.get(name,REPO/name))
            shutil.copyfile(source,package/name)
    manifest['files']=content.inventory(package,('Payload','Runtime','Tools'))
    (package/'package.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    install.package_manifest(package)
    for path in desktop_publish.iterdir():
        if path.is_file() and path.suffix in ('.exe','.dll','.json'):shutil.copyfile(path,app/path.name)
    copy_documents(app,platform_independent=manifest.get('platform_independent',False))
    for name in ('LIESMICH.txt','START-HIER.txt'):(package/name).unlink(missing_ok=True)
    return app


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('stage','output','desktop-publish','dotnet','probe','compiler'):parser.add_argument('--'+name,required=True)
    for name in ('base-package','service','adapter','python-zip','dxvk-dll','dxvk-license','memory-scanner'):parser.add_argument('--'+name)
    parser.add_argument('--easytier-runtime',required=True)
    parser.add_argument('--easytier-source',required=True)
    parser.add_argument('--windows-helpers',required=True)
    parser.add_argument('--update-sequence',type=int,default=27)
    parser.add_argument('--game-directory',action='store_true')
    parser.add_argument('--neo-logistics',action='store_true')
    parser.add_argument('--midsummer-profile',action='store_true')
    parser.add_argument('--atmosphere-addon')
    parser.add_argument('--platform-runtime')
    parser.add_argument('--gold-package')
    values=parser.parse_args()
    build(values.stage,values.output,values.desktop_publish,values.dotnet,values.probe,values.compiler,
        base_package=values.base_package,service=values.service,adapter=values.adapter,python_zip=values.python_zip,dxvk_dll=values.dxvk_dll,dxvk_license=values.dxvk_license,memory_scanner=values.memory_scanner,
        easytier_runtime=values.easytier_runtime,easytier_source=values.easytier_source,windows_helpers=values.windows_helpers,update_sequence=values.update_sequence,game_directory=values.game_directory,neo_logistics=values.neo_logistics,midsummer_profile=values.midsummer_profile,atmosphere_addon=values.atmosphere_addon,platform_runtime=values.platform_runtime,gold_package=values.gold_package)
