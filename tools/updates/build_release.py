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
import release_policy

def build(setup,output,*,version,sequence,tag,notes='',key_path=None):
    setup,output=map(compatibility.plain,(setup,output))
    if not setup.is_file() or setup.name!='Setup.exe' or output.exists() or not output.parent.is_dir():raise ValueError('Use a completed Setup.exe and a new manifest output path.')
    isolated=output.with_name(update_client.ISOLATED_MANIFEST)
    if output.name!=update_client.MANIFEST or isolated.exists():raise ValueError('Use new legacy and isolated release manifest paths.')
    proof=release_policy.read(setup.with_suffix('.release.json'))
    if (proof.get('format')!='weplay-main-package-release-v1' or not release_policy.valid(proof.get('platform_isolation'))
            or proof.get('setup_sha256')!=compatibility.digest(setup) or proof.get('setup_bytes')!=setup.stat().st_size):
        raise ValueError('Signing requires the completed isolated main package and its matching build receipt.')
    value={'format':'weplay-update-v1','repository':update_client.REPOSITORY,'channel':'preview','sequence':sequence,
           'version':version,'tag':tag,'notes':notes,'platform_isolation':release_policy.CONTRACT,'setup':{'name':'Setup.exe',
           'url':'https://github.com/'+update_client.REPOSITORY+'/releases/download/'+tag+'/Setup.exe',
           'size':setup.stat().st_size,'sha256':compatibility.digest(setup)}}
    update_client.validate(value)
    private=compatibility.plain(key_path or ROOT/'work/updates/signing-key.dpapi')
    stage=ROOT/'work/updates';stage.mkdir(exist_ok=True)
    legacy={key:item for key,item in value.items() if key!='platform_isolation'}
    encoded_records=[]
    with tempfile.TemporaryDirectory(prefix='sign-',dir=stage) as temporary:
        for target,record in ((output,legacy),(isolated,value)):
            payload=update_client.canonical(record)
            payload_file=Path(temporary)/'payload.json';payload_file.write_bytes(payload)
            completed=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-File',str(ROOT/'tools/updates/sign_manifest.ps1'),
                '-Action','Sign','-KeyPath',str(private),'-PayloadPath',str(payload_file)],capture_output=True,text=True,timeout=30)
            if completed.returncode:raise ValueError('Signing failed; private key data was not printed.')
            signature=completed.stdout.strip()
            if len(update_client.b64(signature))!=384:raise ValueError('Unexpected signature.')
            envelope={'format':'weplay-signed-update-v1','key_id':update_client.public_key()['key_id'],
                      'payload':base64.urlsafe_b64encode(payload).decode().rstrip('='),'signature':signature}
            encoded=update_client.canonical(envelope)+b'\n';update_client.verify(encoded)
            encoded_records.append((target,encoded))
    for target,encoded in encoded_records:
        with target.open('xb') as stream:stream.write(encoded)
    return {'manifest':str(output),'isolated_manifest':str(isolated),'version':version,'sequence':sequence,'tag':tag,'setup_sha256':value['setup']['sha256'],
            'publish_command':['gh','release','create',tag,str(setup),str(output),str(isolated),'--repo',update_client.REPOSITORY,'--prerelease','--title','WePlay '+version]}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('setup','output','version','tag'):parser.add_argument('--'+name,required=True)
    parser.add_argument('--sequence',required=True,type=int);parser.add_argument('--notes-file',type=Path);parser.add_argument('--key',type=Path)
    args=parser.parse_args()
    notes=args.notes_file.read_text(encoding='utf-8') if args.notes_file else ''
    print(json.dumps(build(args.setup,args.output,version=args.version,sequence=args.sequence,tag=args.tag,notes=notes,key_path=args.key),indent=2))
