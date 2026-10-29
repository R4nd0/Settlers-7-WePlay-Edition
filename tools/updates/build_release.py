"""Sign a complete Setup.exe for the WePlay preview update feed; never uploads."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/installer'))
import compatibility
import update_client

def build(setup,output,*,version,sequence,tag,notes='',key_path=None):
    setup,output=map(compatibility.plain,(setup,output))
    if not setup.is_file() or setup.name!='Setup.exe' or output.exists() or not output.parent.is_dir():raise ValueError('Use a completed Setup.exe and a new manifest output path.')
    value={'format':'weplay-update-v1','repository':update_client.REPOSITORY,'channel':'preview','sequence':sequence,
           'version':version,'tag':tag,'notes':notes,'setup':{'name':'Setup.exe',
           'url':'https://github.com/'+update_client.REPOSITORY+'/releases/download/'+tag+'/Setup.exe',
           'size':setup.stat().st_size,'sha256':compatibility.digest(setup)}}
    update_client.validate(value)
    payload=update_client.canonical(value)
    private=compatibility.plain(key_path or ROOT/'work/updates/signing-key.dpapi')
    stage=ROOT/'work/updates';stage.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sign-',dir=stage) as temporary:
        payload_file=Path(temporary)/'payload.json';payload_file.write_bytes(payload)
        completed=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-File',str(ROOT/'tools/updates/sign_manifest.ps1'),
            '-Action','Sign','-KeyPath',str(private),'-PayloadPath',str(payload_file)],capture_output=True,text=True,timeout=30)
        if completed.returncode:raise ValueError('Signing failed; private key data was not printed.')
        signature=completed.stdout.strip()
        if len(update_client.b64(signature))!=384:raise ValueError('Unexpected signature.')
    envelope={'format':'weplay-signed-update-v1','key_id':update_client.public_key()['key_id'],
              'payload':base64.urlsafe_b64encode(payload).decode().rstrip('='),'signature':signature}
    encoded=update_client.canonical(envelope)+b'\n'
    update_client.verify(encoded)
    with output.open('xb') as stream:stream.write(encoded)
    return {'manifest':str(output),'version':version,'sequence':sequence,'tag':tag,'setup_sha256':value['setup']['sha256'],
            'publish_command':['gh','release','create',tag,str(setup),str(output),'--repo',update_client.REPOSITORY,'--prerelease','--title','WePlay '+version]}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('setup','output','version','tag'):parser.add_argument('--'+name,required=True)
    parser.add_argument('--sequence',required=True,type=int);parser.add_argument('--notes-file',type=Path);parser.add_argument('--key',type=Path)
    args=parser.parse_args()
    notes=args.notes_file.read_text(encoding='utf-8') if args.notes_file else ''
    print(json.dumps(build(args.setup,args.output,version=args.version,sequence=args.sequence,tag=args.tag,notes=notes,key_path=args.key),indent=2))
