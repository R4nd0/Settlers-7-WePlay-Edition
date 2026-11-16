"""Bind reviewed RC4.00 mixed reload receipts to the exact final installer.

This gate checks evidence identity and completeness. It cannot replace the real
game runs or the review of their original native diagnostics.
"""
import hashlib
import json
import re

import compatibility
import crossplay_content
import crossplay_session
import platform_standalone

VERSION='2.0.0-rc4.0'
TAG='v2.0.0-rc4.00'
SEQUENCE=29
CASES=((1,2,'ubisoft'),(1,3,'steam'),(3,1,'steam-host'))
ERROR='RC4.00 signing requires both original History mixed reloads and Steam hosting Gold in this exact final installed package.'


def digest(value,size=64):
    return isinstance(value,str) and bool(re.fullmatch('[0-9a-f]{'+str(size)+'}',value)) and value!='0'*size


def unique(items):
    result={}
    for key,value in items:
        if key in result:raise ValueError(ERROR)
        result[key]=value
    return result


def read(path):
    path=compatibility.plain(path)
    if not path.is_file() or not 1<=path.stat().st_size<=1024*1024:raise ValueError(ERROR)
    raw=path.read_bytes()
    if len(raw)>1024*1024:raise ValueError(ERROR)
    value=json.loads(raw,object_pairs_hook=unique)
    if not isinstance(value,dict):raise ValueError(ERROR)
    return value,hashlib.sha256(raw).hexdigest()


def check(setup,build,*,version,sequence,tag):
    aliases=lambda value:isinstance(value,str) and re.fullmatch(r'(?:v?2\.0\.0-)?rc4\.0+',value.casefold())
    targeted=(sequence==SEQUENCE or bool(aliases(tag)) or bool(aliases(version)))
    if not targeted:return
    try:
        retained,_=read(setup.with_suffix('.release.json'))
        if retained!=build:raise ValueError(ERROR)
        if (version!=VERSION or type(sequence) is not int or sequence!=SEQUENCE or tag!=TAG
                or build.get('crossplay_mixed') is not True or build.get('source_tree_dirty') is not False
                or not digest(build.get('source_commit'),40) or not digest(build.get('package_id'))):
            raise ValueError(ERROR)
        proof,_=read(setup.with_suffix('.mixed-reload.json'))
        expected={'format','version','sequence','tag','setup_sha256','setup_bytes','source_commit',
                  'package_id','catalog_sha256','cases'}
        if (set(proof)!=expected or proof['format']!='weplay-final-mixed-reload-v2'
                or proof['version']!=VERSION or type(proof['sequence']) is not int
                or proof['sequence']!=SEQUENCE or proof['tag']!=TAG
                or type(proof['setup_bytes']) is not int or proof['setup_bytes']!=setup.stat().st_size
                or proof['setup_sha256']!=compatibility.digest(setup)
                or any(proof[name]!=build.get(name) for name in ('setup_sha256','setup_bytes','source_commit','package_id'))
                or proof['catalog_sha256'] not in crossplay_content.APPROVED_DIGESTS
                or not isinstance(proof['cases'],list) or len(proof['cases'])!=len(CASES)):
            raise ValueError(ERROR)
        gold=platform_standalone.GOLD
        history=platform_standalone.HISTORY_PINS[(True,True)]
        for (host,guest,name),record in zip(CASES,proof['cases']):
            edition=guest if host==1 else host
            filename='Setup.mixed-reload-'+name+'.json'
            if (not isinstance(record,dict) or set(record)!={'host_edition','guest_edition','file','sha256'}
                    or type(record['host_edition']) is not int or record['host_edition']!=host
                    or type(record['guest_edition']) is not int or record['guest_edition']!=guest
                    or record['file']!=filename or not digest(record['sha256'])):
                raise ValueError(ERROR)
            case,actual=read(setup.with_name(filename))
            if record['sha256']!=actual:raise ValueError(ERROR)
            keys={'format','host_edition','guest_edition','installed_from_setup_sha256','installed_package_id','source_commit',
                  'catalog_sha256','gold_exe_sha256','history_exe_sha256','gold_adapter_sha256',
                  'history_adapter_sha256','normal_production_launch','fresh_host_save_written',
                  'mixed_reload_completed','continued_logistics_observed','original_crc_source',
                  'shared_crc_blocks','crc_mismatches','synthetic_turns','neo_features','neo_enabled',
                  'neo_state_preserved','gold_exit_code','history_exit_code','forced_termination',
                  'restoration_verified','native_lab_features','reviewed_evidence_sha256'}
            checks=('normal_production_launch','fresh_host_save_written','mixed_reload_completed',
                    'continued_logistics_observed','neo_state_preserved','restoration_verified')
            if (set(case)!=keys or case['format']!='weplay-final-mixed-reload-case-v2'
                    or type(case['host_edition']) is not int or case['host_edition']!=host
                    or type(case['guest_edition']) is not int or case['guest_edition']!=guest
                    or case['installed_from_setup_sha256']!=proof['setup_sha256']
                    or case['installed_package_id']!=proof['package_id']
                    or case['source_commit']!=proof['source_commit']
                    or case['catalog_sha256']!=proof['catalog_sha256']
                    or crossplay_session.EXECUTABLE_EDITIONS.get(case['gold_exe_sha256'])!=1
                    or crossplay_session.EXECUTABLE_EDITIONS.get(case['history_exe_sha256'])!=edition
                    or case['gold_adapter_sha256']!=gold or gold not in crossplay_session.ADAPTER_PINS.get(1,())
                    or case['history_adapter_sha256']!=history or history not in crossplay_session.ADAPTER_PINS.get(edition,())
                    or any(case[key] is not True for key in checks)
                    or case['original_crc_source']!='native-end-of-turn'
                    or type(case['shared_crc_blocks']) is not int or case['shared_crc_blocks']<1
                    or any(type(case[key]) is not int or case[key]!=0 for key in
                           ('crc_mismatches','synthetic_turns','gold_exit_code','history_exit_code'))
                    or type(case['neo_features']) is not int or case['neo_features']!=19
                    or type(case['neo_enabled']) is not bool or case['forced_termination'] is not False
                    or case['native_lab_features'] is not False or not digest(case['reviewed_evidence_sha256'])):
                raise ValueError(ERROR)
    except (OSError,ValueError,TypeError,KeyError,UnicodeError,RecursionError):
        raise ValueError(ERROR) from None
