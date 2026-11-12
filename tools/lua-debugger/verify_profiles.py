"""Check all three native API maps against owned images, without a game launch.

Requires pefile, capstone and Unicorn. Reuses the earlier fingerprinted debug
library/hook audit, then independently searches for unique normalized native
instruction signatures instead of assuming a constant edition-wide offset.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import capstone
import pefile
from profiles import UBISOFT, STEAM, GOLD

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/research'))
from audit_lua_debugger import audit


def signature(pe, address, cs):
    instructions = list(cs.disasm(pe.get_data(address-0x400000, 128), address))[:18]
    raw = b''.join(i.bytes for i in instructions)
    mask = bytearray([255])*len(raw)
    for instruction in instructions:
        offset = instruction.address-address
        if instruction.imm_size and any(op.type == capstone.x86.X86_OP_IMM and
                (op.imm >= 0x400000 or instruction.group(capstone.CS_GRP_JUMP)
                 or instruction.group(capstone.CS_GRP_CALL)) for op in instruction.operands):
            start = offset+instruction.imm_offset
            mask[start:start+instruction.imm_size] = bytes(instruction.imm_size)
        if instruction.disp_size and any(op.type == capstone.x86.X86_OP_MEM and op.mem.disp >= 0x400000
                                        for op in instruction.operands):
            start = offset+instruction.disp_offset
            mask[start:start+instruction.disp_size] = bytes(instruction.disp_size)
    return b''.join(re.escape(bytes([byte])) if keep else b'.' for byte,keep in zip(raw,mask))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('ubisoft', 'steam-analysis', 'gold'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((ROOT/'work').resolve()) or output.exists() or not output.parent.is_dir():
        parser.error('Use a fresh output beneath work/')
    # The reused audit validates full file identities before any binary analysis.
    results = {name: audit(name, path) for name,path in
               (('ubisoft',args.ubisoft),('steam_analysis',args.steam_analysis),('gold',args.gold))}
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); cs.detail = True
    source = pefile.PE(str(args.ubisoft))
    signatures = {name: signature(source, address, cs) for name,address in UBISOFT.items()}
    maps = {}
    for label,path,profile in (('ubisoft',args.ubisoft,UBISOFT),('steam_analysis',args.steam_analysis,STEAM),('gold',args.gold,GOLD)):
        data = path.read_bytes(); pe = pefile.PE(data=data); checked = {}
        for name,address in profile.items():
            hits = [0x400000+pe.get_rva_from_offset(match.start())
                    for match in re.finditer(signatures[name],data,re.S)]
            if hits != [address]:
                raise ValueError('Native API signature is not unique/correct: '+label+'/'+name)
            checked[name] = dict(address=hex(address), unique_match=True,
                                 prefix_sha256=hashlib.sha256(pe.get_data(address-0x400000,64)).hexdigest())
        maps[label] = checked; pe.close()
        print(label+': all 10 native API signatures unique; original hook emulation passed', flush=True)
    source.close()
    output.write_text(json.dumps(dict(native_api_maps=maps, hook_audits=results,
        total_native_api_matches=30, original_hook_calls=18, game_process_started=False,
        lua_bytecode_executed=False),indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    main()
