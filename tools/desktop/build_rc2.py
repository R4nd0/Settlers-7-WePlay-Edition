"""Build the RC3.2 Internet/Discord Preview from pinned inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

REPO=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(REPO/'tools/installer'))
import build_package
import compatibility as content
import install


def build(stage,output,desktop_publish,dotnet,probe,compiler,*,base_package=None,service=None,python_zip=None,dxvk_dll=None,dxvk_license=None,easytier_runtime=None,easytier_source=None):
    stage,output,desktop_publish,dotnet,probe,compiler=map(content.plain,(stage,output,desktop_publish,dotnet,probe,compiler))
    if stage.exists() or not stage.parent.is_dir():raise ValueError('Use a new stage directory')
    if not stage.is_relative_to(REPO/'work'):raise ValueError('Stage must be inside work')
    if not output.is_relative_to(REPO/'outputs'):raise ValueError('Preview output must be inside outputs')
    if easytier_runtime is not None and easytier_source is None:raise ValueError('Corresponding EasyTier source is required')
    if not compiler.is_file() or not probe.is_file():raise ValueError('Build tools missing')
    if not (desktop_publish/'S7Launcher.exe').is_file():raise ValueError('Desktop app publish is missing')
    stage.mkdir();output.mkdir(parents=True,exist_ok=True)
    app=stage/'App';app.mkdir()
    for path in desktop_publish.iterdir():
        content.plain(path)
        if path.is_file() and path.suffix in ('.exe','.dll','.json'):shutil.copyfile(path,app/path.name)
    version='10.0.13'
    runtime=app/'DesktopRuntime';runtime.mkdir()
    for name in ('dotnet.exe','LICENSE.txt','ThirdPartyNotices.txt'):shutil.copyfile(dotnet/name,runtime/name)
    for relative in ('host/fxr/'+version,'shared/Microsoft.NETCore.App/'+version,'shared/Microsoft.WindowsDesktop.App/'+version):
        shutil.copytree(dotnet/relative,runtime/relative)
    old=content.plain(base_package or REPO/'outputs/S7-AutoLAN-Testpaket-2026-10-28-r3')
    install.package_manifest(old)
    build_package.build(old/'Payload/uplay_r1_loader.dll',service or old/'Payload/Server',old/'Runtime/dotnet',
        python_zip or REPO/'work/installer-runtime/python-3.14.10-embed-amd64.zip',app/'Package',
        dxvk_dll=dxvk_dll or REPO/'work/dxvk-3.1.1-x32/d3d9.dll',
        dxvk_license=dxvk_license or REPO/'work/dxvk-3.1.1-x32/LICENSE',graphics_probe=probe,
        easytier_runtime=easytier_runtime)
    # CMDs are private development entrypoints, never part of the new user flow.
    # They are not removed from old packages or installed game copies.
    for path in (app/'Package').glob('*.cmd'):path.unlink()
    for name in ('LIESMICH.txt','START-HIER.txt'):(app/'Package'/name).unlink(missing_ok=True)
    guide=REPO/'docs/RC3-INTERNET.md'
    if guide.is_file():shutil.copyfile(guide,app/'LIESMICH.txt')
    if (REPO/'docs/DISCORD-INTEGRATION.md').is_file():shutil.copyfile(REPO/'docs/DISCORD-INTEGRATION.md',app/'DISCORD-INTEGRATION.md')
    if easytier_runtime is not None:
        sys.path.insert(0,str(REPO/'tools/easytier'))
        import source_bundle
        sources=app/'Source';sources.mkdir()
        source_bundle.build(easytier_source,sources/'EasyTier-corresponding-source.zip')
        shutil.copyfile(sources/'EasyTier-corresponding-source.zip',output/'EasyTier-corresponding-source.zip')
    release={'format':'s7-desktop-release-v1','release':'RC3.2 External Discord Preview','network_revision':4,'game_bits':32,
             'internet_transport':'EasyTier 2.6.4-weplay-npcap-free' if easytier_runtime is not None else None,
             'internet_gameplay_verified':False,
             'project':'Settlers-7-WePlay-Edition',
             'source_repository':'https://github.com/R4nd0/Settlers-7-WePlay-Edition',
             'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip(),
             'source_tree_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=REPO,text=True).strip()),
             'package_id':content.fingerprint(install.package_manifest(app/'Package')['files']),
             'probe_sha256':content.digest(probe),'compiler':'Inno Setup 7.1.1 x64','authenticode_signed':False}
    (app/'desktop-release.json').write_text(json.dumps(release,indent=2)+'\n',encoding='utf-8')
    compile_setup(app,output,compiler)
    return app


def compile_setup(app,output,compiler):
    if any((app/name).exists() for name in ('State','Instances','LocalData')):
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
    for name in manifest['files']:
        if name.startswith('tools/') and name!='tools/license.txt':
            source=content.plain(REPO/name)
            shutil.copyfile(source,package/name)
    manifest['files']=content.inventory(package,('Payload','Runtime','Tools'))
    (package/'package.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    install.package_manifest(package)
    for path in desktop_publish.iterdir():
        if path.is_file() and path.suffix in ('.exe','.dll','.json'):shutil.copyfile(path,app/path.name)
    if (REPO/'docs/RC3-INTERNET.md').is_file():shutil.copyfile(REPO/'docs/RC3-INTERNET.md',app/'LIESMICH.txt')
    if (REPO/'docs/DISCORD-INTEGRATION.md').is_file():shutil.copyfile(REPO/'docs/DISCORD-INTEGRATION.md',app/'DISCORD-INTEGRATION.md')
    for name in ('LIESMICH.txt','START-HIER.txt'):(package/name).unlink(missing_ok=True)
    return app


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('stage','output','desktop-publish','dotnet','probe','compiler'):parser.add_argument('--'+name,required=True)
    for name in ('base-package','service','python-zip','dxvk-dll','dxvk-license'):parser.add_argument('--'+name)
    parser.add_argument('--easytier-runtime',required=True)
    parser.add_argument('--easytier-source',required=True)
    values=parser.parse_args()
    build(values.stage,values.output,values.desktop_publish,values.dotnet,values.probe,values.compiler,
        base_package=values.base_package,service=values.service,python_zip=values.python_zip,dxvk_dll=values.dxvk_dll,dxvk_license=values.dxvk_license,
        easytier_runtime=values.easytier_runtime,easytier_source=values.easytier_source)
