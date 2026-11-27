"""Build pinned x86 On12 runtime variants for explicit local experiments."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import urllib.request
import zipfile

from . import recover

ROOT=Path(__file__).resolve().parents[2]
CONFIG=dict(platform='Win32',configuration='Release',sdk='10.0.26100.0',toolset='14.50.35717')
REVISIONS={
    'D3D9On12':'9543ac633c8c73b31f6a630869bc769b89dbdebc',
    'D3D12TranslationLayer':'4b9c8f8a49c9b68df19be817d56f123d65704fd2',
    'DirectX-Headers':'9e393d6d8a3b30dcc6f2806ef604ec16a27b0d7e',
}
SOURCE_PINS={
    'D3D9On12':'75e693b1d2212f8020bd2b63aac37f7876204f6c9b7539e4052e4ba43bb011ce',
    'D3D12TranslationLayer':'9be2648e9a9e956949777d6fadc4379df6b9bdbe0eeab23773afde90c5c11f7b',
    'DirectX-Headers':'199eefdcb026c242d6bd12c85fd0ae601f031f7144c879cf9883eb1e3d0d0b2a',
}
PACKAGES={
    'wdk':('https://api.nuget.org/v3-flatcontainer/microsoft.windows.wdk.x86/10.0.26100.6584/microsoft.windows.wdk.x86.10.0.26100.6584.nupkg','dfa6cf4884fc7b88d59e2d40afad876bc6b3f4954bdf449d5c0ded9e3db22db1'),
    'signer':('https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.dxbcsigner/1.0.0/microsoft.direct3d.dxbcsigner.1.0.0.nupkg','ad17eb3b9edf6a92462c7692343dfca4d35a306f72e1ee6f889bbf3a6d9c1de8'),
    'pix':('https://api.nuget.org/v3-flatcontainer/winpixeventruntime/1.0.190604001/winpixeventruntime.1.0.190604001.nupkg','2496f49c819d2162a16f116c69146a256ede445daecc57f2b7fdeeaccf440981'),
    'atl-headers':('https://download.visualstudio.microsoft.com/download/pr/eef0996f-4334-4b1a-a344-4d13e27ac052/64e638a9e62e056a14b16cf66c2d7710291a6d408953065e2a8caf45ab995e42/Microsoft.VC.14.50.18.0.ATL.Headers.base.vsix','64e638a9e62e056a14b16cf66c2d7710291a6d408953065e2a8caf45ab995e42'),
    'atl-x86':('https://download.visualstudio.microsoft.com/download/pr/eef0996f-4334-4b1a-a344-4d13e27ac052/31401fe8b051ce1cde1610fc6e5baec818e9b13ab335f3b71a323b3b8ba96398/Microsoft.VC.14.50.18.0.ATL.X86.base.vsix','31401fe8b051ce1cde1610fc6e5baec818e9b13ab335f3b71a323b3b8ba96398'),
}
PROJECT_INPUTS=('native/on12_constant_reuse_gpu.cpp','native/water_on12_identity.h')
VARIANTS=('stock','water-worker')
MANIFEST='on12-runtime-build.json'
FILES=('d3d9on12.dll','dxbcSigner.dll','on12_runtime_gpu.exe')

def digest(path):
    return hashlib.sha256(recover._read(recover.plain(path),64*1024*1024)[0]).hexdigest()

def relative(name):
    if not isinstance(name,str) or '\\' in name or ':' in name:
        raise ValueError('Invalid build input name.')
    parts=PurePosixPath(name)
    if parts.is_absolute() or not parts.parts or any(x in ('.','..') for x in name.split('/')):
        raise ValueError('Invalid build input name.')
    return parts

def inputs(directory):
    result={}
    for folder in ('sources','dependencies','project'):
        for path in sorted((directory/folder).rglob('*')):
            path=recover.plain(path)
            if path.is_file():result[path.relative_to(directory).as_posix()]=digest(path)
    return result

def project_inputs(variant):
    if variant not in VARIANTS:raise ValueError('Unknown On12 source variant.')
    if variant=='stock':return PROJECT_INPUTS
    from . import on12_water_patch
    return (*PROJECT_INPUTS,*on12_water_patch.INPUTS)


def project_cmake(directory,variant='stock'):
    project_inputs(variant)
    gpu=(ROOT/PROJECT_INPUTS[0]).as_posix();source=(directory/'sources/D3D9On12').as_posix();binary=(directory/'bin').as_posix()
    extra=f'target_include_directories(d3d9on12 PRIVATE "{(ROOT/"native").as_posix()}")\n' if variant=='water-worker' else ''
    return f'''cmake_minimum_required(VERSION 3.18)
project(S7On12LocalRuntime)
add_subdirectory("{source}" on12)
{extra}
set_target_properties(d3d9on12 PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "{binary}")
add_executable(on12_runtime_gpu "{gpu}")
target_compile_features(on12_runtime_gpu PRIVATE cxx_std_17)
target_compile_options(on12_runtime_gpu PRIVATE /W4 /WX /O2 /fp:strict)
target_link_libraries(on12_runtime_gpu PRIVATE bcrypt user32)
set_target_properties(on12_runtime_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "{binary}")
'''

def expected_inputs(directory,variant='stock'):
    """Bind extracted files to the trusted archives, not mutable manifest claims."""
    result={'project/CMakeLists.txt':hashlib.sha256(project_cmake(directory,variant).encode()).hexdigest()}
    from . import on12_water_patch
    for name,pin,folder,strip in [*( (n,p,'sources/'+n,True) for n,p in SOURCE_PINS.items()),
                               *( (n,p,'dependencies/'+n,False) for n,(_,p) in PACKAGES.items())]:
        archive=directory/'archives'/(name+'.zip')
        if digest(archive)!=pin:raise ValueError('Pinned source archive changed.')
        prefix=None
        with zipfile.ZipFile(archive) as source:
            for entry in source.infolist():
                parts=relative(entry.filename.rstrip('/')).parts
                if strip:
                    if prefix is None:prefix=parts[0]
                    if parts[0]!=prefix:raise ValueError('Unexpected archive root.')
                    parts=parts[1:]
                if not parts or entry.is_dir():continue
                key=folder+'/'+str(PurePosixPath(*parts))
                if key in result:raise ValueError('Duplicate archive input.')
                raw=source.read(entry)
                if variant=='water-worker' and folder=='sources/D3D9On12' and str(PurePosixPath(*parts)) in on12_water_patch.EDITS:
                    raw=on12_water_patch.patched(str(PurePosixPath(*parts)),raw)
                result[key]=hashlib.sha256(raw).hexdigest()
    if variant=='water-worker':
        for name,path in on12_water_patch.ADDED.items():result['sources/D3D9On12/'+name]=digest(ROOT/path)
    return result

def download(url,pin,path):
    request=urllib.request.Request(url,headers={'User-Agent':'S7-On12-local-build'})
    with urllib.request.urlopen(request,timeout=30) as response:raw=response.read(32*1024*1024+1)
    if len(raw)>32*1024*1024 or hashlib.sha256(raw).hexdigest()!=pin:
        raise ValueError('Pinned build archive integrity failed.')
    recover._write_new(path,raw)

def unpack(archive,destination,*,strip_root=False):
    destination.mkdir(parents=True)
    with zipfile.ZipFile(archive) as source:
        names=source.infolist();prefix=None
        for entry in names:
            parts=relative(entry.filename.rstrip('/')).parts
            if strip_root:
                if prefix is None:prefix=parts[0]
                if parts[0]!=prefix:raise ValueError('Unexpected archive root.')
                parts=parts[1:]
            if not parts:continue
            if (entry.external_attr>>16)&0o170000==0o120000:raise ValueError('Archive links are not supported.')
            target=recover.plain(destination.joinpath(*parts))
            if not target.is_relative_to(destination):raise ValueError('Archive escaped its destination.')
            if entry.is_dir():target.mkdir(parents=True,exist_ok=True)
            else:
                target.parent.mkdir(parents=True,exist_ok=True)
                recover._write_new(target,source.read(entry))

def build(directory,*,variant='stock'):
    project_inputs(variant)
    if os.name!='nt':raise ValueError('The x86 runtime build requires Windows and VS 2026 Build Tools.')
    directory=recover.lab(directory)
    if directory.exists():raise ValueError('Use a fresh directory below work/.')
    finder=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
    installation=Path(subprocess.check_output([str(finder),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip())
    cmake=installation/'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    if not cmake.is_file() or not (installation/'VC/Tools/MSVC'/CONFIG['toolset']).is_dir():
        raise ValueError('The pinned VS 2026 x86 toolset is unavailable.')
    directory.mkdir();archives=directory/'archives';archives.mkdir()
    for name,revision in REVISIONS.items():
        archive=archives/(name+'.zip');download(f'https://codeload.github.com/microsoft/{name}/zip/{revision}',SOURCE_PINS[name],archive)
        unpack(archive,directory/'sources'/name,strip_root=True)
    for name,(url,pin) in PACKAGES.items():
        archive=archives/(name+'.zip');download(url,pin,archive);unpack(archive,directory/'dependencies'/name)
    if variant=='water-worker':
        from . import on12_water_patch
        on12_water_patch.apply(directory)
    project=directory/'project';project.mkdir();output=directory/'build'
    output.mkdir();packages=output/'packages';packages.mkdir()
    shutil.copytree(directory/'dependencies/signer',packages/'Microsoft.Direct3D.DxbcSigner.1.0.0')
    shutil.copytree(directory/'dependencies/pix',packages/'WinPixEventRuntime.1.0.190604001')
    recover._write_new(project/'CMakeLists.txt',project_cmake(directory,variant).encode())
    include=next((directory/'dependencies/atl-headers').rglob('atlstr.h')).parent
    library=next((directory/'dependencies/atl-x86').rglob('atls.lib')).parent
    wdk=directory/'dependencies/wdk/c/Include/10.0.26100.0'
    before=inputs(directory);project_pins={name:digest(ROOT/name) for name in project_inputs(variant)}
    if before!=expected_inputs(directory,variant):raise RuntimeError('Extracted build inputs differ from their pinned archives and exact variant.')
    arguments=[str(cmake),'-S',str(project),'-B',str(output),'-G','Visual Studio 18 2026','-A','Win32','-T','version='+CONFIG['toolset'],
        '-DCMAKE_SYSTEM_VERSION='+CONFIG['sdk'],f'-DFETCHCONTENT_SOURCE_DIR_D3D12TRANSLATIONLAYER={(directory/"sources/D3D12TranslationLayer").as_posix()}',
        f'-DFETCHCONTENT_SOURCE_DIR_DIRECTX-HEADERS={(directory/"sources/DirectX-Headers").as_posix()}',
        f'-DCMAKE_CXX_FLAGS=/EHsc /I"{wdk.as_posix()}/um" /I"{wdk.as_posix()}/shared" /I"{include.as_posix()}"',
        f'-DCMAKE_SHARED_LINKER_FLAGS=/LIBPATH:"{library.as_posix()}"', '-DFETCHCONTENT_FULLY_DISCONNECTED=ON','-DCMAKE_SUPPRESS_REGENERATION=ON']
    environment=dict(os.environ,MSBUILDDISABLENODEREUSE='1')
    with (directory/'build.private.log').open('xb') as log:
        subprocess.run(arguments,stdout=log,stderr=subprocess.STDOUT,env=environment,check=True)
        subprocess.run([str(cmake),'--build',str(output),'--config','Release','--parallel','1','--','/nr:false',f'/p:ATL_KeyFile={library.as_posix()}/atls.lib','/p:DirectXPlatform=win32'],stdout=log,stderr=subprocess.STDOUT,env=environment,check=True)
    if inputs(directory)!=before or any(digest(ROOT/name)!=pin for name,pin in project_pins.items()):
        raise RuntimeError('Build inputs changed during compilation.')
    for name in ('d3d9on12.dll','on12_runtime_gpu.exe'):shutil.copyfile(directory/'bin'/name,directory/name)
    shutil.copyfile(directory/'dependencies/signer/build/native/bin/win32/dxbcSigner.dll',directory/'dxbcSigner.dll')
    record=dict(format='s7-on12-runtime-build-v1',variant=variant,configuration=CONFIG,revisions=REVISIONS,
        source_archives=SOURCE_PINS,packages={name:pin for name,(_,pin) in PACKAGES.items()},builder_sha256=digest(Path(__file__)),
        project_inputs=project_pins,inputs=before,files={name:digest(directory/name) for name in FILES})
    recover._write_new(directory/MANIFEST,json.dumps(record,indent=2).encode())
    return directory

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path)
    parser.add_argument('--variant',choices=VARIANTS,default='stock')
    args=parser.parse_args();print(json.dumps({'build':str(build(args.output,variant=args.variant))}))

if __name__=='__main__':main()
