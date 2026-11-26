"""Persist a bounded, unqualified combined shader choice; never activate a release."""
from __future__ import annotations
import json
import os
from pathlib import Path

KEYS={1:'water',2:'vegetation',3:'terrain',4:'shadow',5:'ambient'}
LIMITS={1:2,2:2,3:0,4:0,5:1}


def candidate(report):
    if (not isinstance(report,dict) or report.get('format')!='s7-supersampling-hook-v1'
            or report.get('overlay_profile')!=6):
        raise ValueError('An explicit combined native report is required.')
    rows=report.get('overlay_preset');revision=report.get('overlay_preset_revision')
    if type(revision)is not int or not 0<=revision<2**64 or not isinstance(rows,list) or len(rows)!=5:
        raise ValueError('Incomplete combined shader selection.')
    selection={}
    for expected,row in enumerate(rows,1):
        if not isinstance(row,dict) or set(row)!={'profile','enabled','variant'}:
            raise ValueError('Unexpected choice fields.')
        profile,enabled,variant=(row[k]for k in ('profile','enabled','variant'))
        if (type(profile)is not int or profile!=expected or type(enabled)is not int or enabled not in(0,1)
                or type(variant)is not int or not 0<=variant<=LIMITS[profile]):
            raise ValueError('Invalid or reordered fixed shader choice.')
        selection[KEYS[profile]]=dict(enabled=bool(enabled),variant=variant)
    return dict(format='s7-native-shader-selection-v1',revision=revision,selection=selection,
                release_default_requested=True,selection_confirmed_by_user=False,release_qualified=False,
                optical_acceptance_pending=True)


def _atomic(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_name(path.name+'.tmp')
    with temporary.open('w',encoding='utf-8',newline='\n')as stream:
        json.dump(value,stream,ensure_ascii=False,indent=2);stream.write('\n')
    os.replace(temporary,path)


class Recorder:
    def __init__(self,report_path,output_path,*,mirror=None,source=None):
        self.report=Path(report_path);self.output=Path(output_path)
        self.mirror=Path(mirror)if mirror is not None else None
        self.source=dict(source or {})
        self.last=None;self.rejected=0

    def update(self):
        try:
            with self.report.open('rb')as stream:raw=stream.read(1024*1024+1)
            if len(raw)>1024*1024:raise ValueError('Native report exceeds bound.')
            value=candidate(json.loads(raw))
            if self.source:value['source']=self.source
        except (OSError,ValueError,TypeError):
            self.rejected+=1
            return False
        key=(value['revision'],json.dumps(value['selection'],sort_keys=True))
        if self.last==key:return False
        if self.last is not None and value['revision']<self.last[0]:
            self.rejected+=1;return False
        try:
            _atomic(self.output,value)
        except OSError:
            self.rejected+=1;return False
        if self.mirror is not None:
            try:_atomic(self.mirror,value)
            except OSError:self.rejected+=1
        self.last=key
        return True
