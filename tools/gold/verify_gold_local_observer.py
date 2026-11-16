"""Verify Gold local-queue observer ABI, owned release and original-byte composition.

Synthetic native roots/events and original lock/queue continuation are modeled.
Original game code never executes. Windows imports are modeled and the C++
exception path is excluded. Requires pefile/unicorn and a pinned original Gold image.
"""
from pathlib import Path
import argparse,json,hashlib
import pefile
from emulate_event_payloads import Image
from verify_codec_guards import check_image
from verify_start_fixture import run

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gold',type=Path,required=True)
parser.add_argument('--thunk-executable',type=Path,required=True)
parser.add_argument('output',type=Path)
args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():
    parser.error('Use a fresh output under repository work')
raw=args.thunk_executable.read_bytes();fixture=pefile.PE(data=raw)

cases=[(0,0),(2,0),(1,0),*((0,variant) for variant in range(1,10))]
for arguments in cases:run(fixture,b'WePlayLocalABICase',arguments)
for variant in range(6):run(fixture,b'WePlayLocalReleaseCase',(variant,))
image=Image('gold',args.gold,types=())
guards=check_image(image,fixture,1,oracle=b'WePlayGoldLocalMethodsOracle',methods=((0xa4e430,0xe1),))
for variant in range(9):run(fixture,b'WePlayGoldLocalCompositeCase',(variant,),image=image)
receipt={'abi_cases':len(cases),'owned_release_cases':6,'original_dispatcher_composition_cases':9,
         'guard_cases':guards,'fixture_sha256':hashlib.sha256(raw).hexdigest(),
         'original_lock_and_queue_continuation_modeled':True,'windows_imports_modeled':True,
         'original_game_code_executed':False,'cpp_exception_path_emulated':False,'game_started':False}
with out.open('x') as output:json.dump(receipt,output,indent=2)
print(json.dumps(receipt))
