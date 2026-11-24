"""PID-scoped PresentMon measurement for an admitted active graphics case.

Never sends input, writes game memory or terminates a game. GPU readbacks and
effect-epoch changes invalidate the interval instead of being hidden from it.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import time
import uuid

from . import reconfigure as scope
import observe_game as telemetry


def interval_identity(record):
    fields=('capture_requests','capture_completed','capture_failures',
            'effects_revision','effects_active','effects_pending')
    if not isinstance(record,dict) or any(type(record.get(key))is not int for key in fields):
        raise ValueError('A complete native frame/effects observation is required.')
    if record['effects_pending'] or record['capture_requests']!=record['capture_completed']+record['capture_failures']:
        raise ValueError('Wait for pending captures/material changes before measuring.')
    return tuple(record[key] for key in fields)


def measure(case,dll,output,presentmon,*,seconds=30):
    if type(seconds)is not int or not 10<=seconds<=120:
        raise ValueError('Use a bounded 10–120 second interval.')
    files=scope._case_files(case,dll)
    output=scope._lab(output)
    if output.exists() or not output.parent.is_dir():
        raise ValueError('Use a fresh private pacing directory.')
    collector=telemetry.validate_presentmon(presentmon)
    api=scope.isolated_process.Windows();scope._bind(api)
    handle=api.check(api.k.OpenProcess(0x410,False,files['owned']['pid']))
    capture=None
    def owned():
        if scope._digest(scope._read(files['case']/'owned.private.json',16384))!=files['record_sha256']:
            raise RuntimeError('The admitted case changed during measurement.')
        return scope.validate_live_state(scope._live_state(api,handle,files['owned']['pid'],files['owned']['parent']),files)
    try:
        creation=owned()
        before=scope._json(files['case']/'hook-state.private.json')
        identity=interval_identity(before)
        output.mkdir()
        with (output/'presentmon.log').open('xb')as log:
            started=time.monotonic_ns()
            capture=subprocess.Popen(telemetry.presentmon_command(collector,files['owned']['pid'],seconds,
                output/'frames.csv','S7Graphics-'+uuid.uuid4().hex),stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW)
            deadline=time.monotonic()+seconds+15
            while capture.poll()is None:
                if time.monotonic()>deadline:
                    raise RuntimeError('The owned collector exceeded its bounded lifetime.')
                if owned()!=creation:
                    raise RuntimeError('The original process changed during measurement.')
                time.sleep(1)
        after=scope._json(files['case']/'hook-state.private.json')
        stable=interval_identity(after)==identity
        frames=telemetry.summarize_frames(output/'frames.csv',files['owned']['pid'])
        diagnostics=telemetry.collector_diagnostics(output/'presentmon.log')
        complete=(capture.returncode==0 and diagnostics['recording_started'] and diagnostics['recording_stopped']
            and not diagnostics['event_overflow_warning'] and not diagnostics['etw_events_lost']
            and not diagnostics['log_truncated'] and frames['status']=='frames-observed' and stable)
        result=dict(format='s7-native-graphics-pacing-v1',identity=files['owned'],
            observer_started_monotonic_ns=started,observer_finished_monotonic_ns=time.monotonic_ns(),
            seconds=seconds,collector_sha256=telemetry.PRESENTMON_SHA,collector_exit_code=capture.returncode,
            capture_and_effect_epoch_stable=stable,measurement_complete=complete,
            game_modified=False,game_terminated=False,frames=frames,diagnostics=diagnostics,
            native_before=before,native_after=after)
        (output/'receipt.private.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        return result
    finally:
        if capture and capture.poll()is None:
            capture.terminate();capture.wait(timeout=5) # Only our collector.
        api.k.CloseHandle(handle)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('case','dll','output','presentmon'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--seconds',type=int,default=30)
    args=parser.parse_args()
    result=measure(args.case,args.dll,args.output,args.presentmon,seconds=args.seconds)
    print(json.dumps(dict(measurement_complete=result['measurement_complete'],frames=result['frames'])))


if __name__=='__main__':main()
