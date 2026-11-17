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
CASES=((1,'gold-host'),(3,'steam-host'))
EDITIONS=(1,2,3)
ERROR='RC4.00 signing requires simultaneous original Gold, Ubisoft History and Steam History clients plus one AI opponent, with saved reload and stable continuation in this exact final installed package.'


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
        if (set(proof)!=expected or proof['format']!='weplay-final-mixed-reload-v3'
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
        for (host,name),record in zip(CASES,proof['cases']):
            filename='Setup.mixed-reload-'+name+'.json'
            if (not isinstance(record,dict) or set(record)!={'host_edition','file','sha256'}
                    or type(record['host_edition']) is not int or record['host_edition']!=host
                    or record['file']!=filename or not digest(record['sha256'])):
                raise ValueError(ERROR)
            case,actual=read(setup.with_name(filename))
            if record['sha256']!=actual:raise ValueError(ERROR)
            keys={'format','host_edition','installed_from_setup_sha256','installed_package_id','source_commit',
                  'catalog_sha256','participants','human_players','ai_opponents','all_clients_simultaneous',
                  'session_sha256','reload_session_sha256','host_save_sha256','normal_speed',
                  'normal_production_launch','fresh_host_save_written','fresh_reload_processes',
                  'mixed_reload_completed','continued_logistics_observed','original_crc_source',
                  'crc_compared_editions','shared_crc_blocks_before_save','shared_crc_blocks_after_reload',
                  'post_reload_duration_ms','stability_review_passed','crc_mismatches','synthetic_turns','neo_features','neo_enabled',
                  'neo_state_preserved','forced_termination',
                  'restoration_verified','native_lab_features','reviewed_evidence_sha256'}
            checks=('normal_production_launch','fresh_host_save_written','mixed_reload_completed',
                    'continued_logistics_observed','neo_state_preserved','restoration_verified',
                    'fresh_reload_processes','all_clients_simultaneous','normal_speed','stability_review_passed')
            if (set(case)!=keys or case['format']!='weplay-final-mixed-reload-case-v3'
                    or type(case['host_edition']) is not int or case['host_edition']!=host
                    or case['installed_from_setup_sha256']!=proof['setup_sha256']
                    or case['installed_package_id']!=proof['package_id']
                    or case['source_commit']!=proof['source_commit']
                    or case['catalog_sha256']!=proof['catalog_sha256']
                    or type(case['human_players']) is not int or case['human_players']!=3
                    or type(case['ai_opponents']) is not int or case['ai_opponents']!=1
                    or not digest(case['session_sha256']) or not digest(case['reload_session_sha256'])
                    or case['session_sha256']==case['reload_session_sha256'] or not digest(case['host_save_sha256'])
                    or any(case[key] is not True for key in checks)
                    or case['original_crc_source']!='native-end-of-turn'
                    or case['crc_compared_editions']!=list(EDITIONS)
                    or any(type(edition) is not int for edition in case['crc_compared_editions'])
                    or any(type(case[key]) is not int or case[key]<1 for key in
                           ('shared_crc_blocks_before_save','shared_crc_blocks_after_reload','post_reload_duration_ms'))
                    or any(type(case[key]) is not int or case[key]!=0 for key in
                           ('crc_mismatches','synthetic_turns'))
                    or type(case['neo_features']) is not int or case['neo_features']!=19
                    or type(case['neo_enabled']) is not bool or case['forced_termination'] is not False
                    or case['native_lab_features'] is not False or not digest(case['reviewed_evidence_sha256'])):
                raise ValueError(ERROR)
            participants=case['participants']
            if not isinstance(participants,list) or len(participants)!=3:raise ValueError(ERROR)
            for edition,participant in zip(EDITIONS,participants):
                adapter=gold if edition==1 else history
                participant_keys={'edition','role','exe_sha256','adapter_sha256','installed_from_setup_sha256',
                                  'installed_package_id','session_sha256','reload_session_sha256','loaded_host_save_sha256',
                                  'original_save_loader_used','continued_logistics_observed','ai_opponent_observed',
                                  'neo_state_preserved','exit_code'}
                if (not isinstance(participant,dict) or set(participant)!=participant_keys
                        or type(participant['edition']) is not int or participant['edition']!=edition
                        or participant['role']!=('host' if edition==host else 'guest')
                        or crossplay_session.EXECUTABLE_EDITIONS.get(participant['exe_sha256'])!=edition
                        or participant['adapter_sha256']!=adapter
                        or adapter not in crossplay_session.ADAPTER_PINS.get(edition,())
                        or participant['installed_from_setup_sha256']!=proof['setup_sha256']
                        or participant['installed_package_id']!=proof['package_id']
                        or any(participant[key]!=case[key] for key in ('session_sha256','reload_session_sha256'))
                        or participant['loaded_host_save_sha256']!=case['host_save_sha256']
                        or any(participant[key] is not True for key in
                               ('original_save_loader_used','continued_logistics_observed','ai_opponent_observed','neo_state_preserved'))
                        or type(participant['exit_code']) is not int or participant['exit_code']!=0):
                    raise ValueError(ERROR)
    except (OSError,ValueError,TypeError,KeyError,UnicodeError,RecursionError):
        raise ValueError(ERROR) from None
