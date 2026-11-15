"""Package the qualified no-GUI x86 runtime and its pinned Vulkan manifest."""
import argparse
import hashlib
import io
from pathlib import Path
import sys
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'installer'))
import compatibility as content
import reshade_runtime as runtime


def bundle(dll,manifest,output):
    dll,manifest,output=map(content.plain,(dll,manifest,output))
    if output.exists():raise ValueError('Use a new output file')
    members=(('ReShade32.dll',dll.read_bytes(),runtime.DLL_SHA256),
             ('ReShade32.json',manifest.read_bytes(),runtime.MANIFEST_SHA256))
    if any(hashlib.sha256(raw).hexdigest()!=pin for _,raw,pin in members):
        raise ValueError('Unqualified quiet ReShade runtime or Vulkan manifest')
    packed=io.BytesIO()
    with zipfile.ZipFile(packed,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name,raw,_ in members:
            info=zipfile.ZipInfo(name,(2026,11,15,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,raw)
    raw=packed.getvalue()
    if hashlib.sha256(raw).hexdigest()!=runtime.SETUP_SHA256:
        raise ValueError('Quiet ReShade bundle fingerprint mismatch')
    with output.open('xb') as stream:stream.write(raw)
    return runtime.SETUP_SHA256


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('dll','manifest','output'):parser.add_argument('--'+name,required=True)
    args=parser.parse_args()
    print(bundle(args.dll,args.manifest,args.output))
