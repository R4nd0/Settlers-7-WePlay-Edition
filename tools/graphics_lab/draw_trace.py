"""Read-only next-frame draw order and bounded pixel ROI in an existing owned case."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
from . import load_hook, reconfigure
from .quick_compare import memory_healthy


def configuration(sequence, *, first_draw=0, draw_count=0, target=(0, 0), roi=(0, 0, 0, 0)):
    values = (sequence, first_draw, draw_count, *target, *roi)
    if len(values) != 9 or any(type(v) is not int or v < 0 or v > 0xffffffff for v in values):
        raise ValueError('Use bounded integer trace fields.')
    if not 1 <= sequence <= 1_000_000:
        raise ValueError('Use a bounded unique trace sequence.')
    if draw_count:
        w, h = target
        x, y, rw, rh = roi
        if (not 1 <= draw_count <= 8 or not 1 <= first_draw <= 32769 - draw_count
                or not 1 <= w <= 3840 or not 1 <= h <= 2160
                or not 1 <= rw <= min(256, w) or not 1 <= rh <= min(256, h)
                or x > w - rw or y > h - rh):
            raise ValueError('Invalid draw range, target dimensions or bounded ROI.')
    elif any(values[1:]):
        raise ValueError('An inventory request must have all pixel fields zero.')
    return struct.pack('<11I', 44, 2, *values)


def trace(case, dll, sequence, **selection):
    payload = configuration(sequence, **selection)
    files = reconfigure._case_files(case, dll)
    memory_healthy(json.loads((files['case'] / 'memory-live.private.json').read_text()), files, time.monotonic_ns())
    request = files['case'] / f'draw-trace-{sequence}-request.private.json'
    receipt = files['case'] / f'draw-trace-{sequence}-receipt.private.json'
    if request.exists() or receipt.exists():
        raise ValueError('Keep previous trace records; use a new sequence.')
    owner = reconfigure._CaseOwner(files)
    try:
        with request.open('x', encoding='utf-8') as stream:
            json.dump(dict(sequence=sequence, owned_record_sha256=files['record_sha256'],
                           config_sha256=hashlib.sha256(payload).hexdigest(),
                           **selection), stream, indent=2)
        result = load_hook.invoke(owner, files['dll'], 'draw_trace', payload)
        with receipt.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2)
        output = files['case'] / f'material-observer.private.json.trace-{sequence}.json'
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if output.exists():
                try:
                    data = json.loads(output.read_text())
                except (ValueError, OSError):
                    time.sleep(.05)
                    continue
                if data.get('sequence') != sequence or data.get('format') != 's7-original-draw-trace-v2':
                    raise ValueError('Trace receipt identity mismatch.')
                return data
            if (files['case'] / 'result.private.json').exists():
                raise RuntimeError('The owned game ended before its traced frame completed.')
            time.sleep(.05)
        raise TimeoutError('The owned game did not complete its trace within 30 seconds.')
    finally:
        owner.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case', required=True, type=Path)
    p.add_argument('--dll', required=True, type=Path)
    p.add_argument('--sequence', required=True, type=int)
    p.add_argument('--first-draw', type=int, default=0)
    p.add_argument('--draw-count', type=int, default=0)
    p.add_argument('--target', type=int, nargs=2, default=(0, 0))
    p.add_argument('--roi', type=int, nargs=4, default=(0, 0, 0, 0))
    a = p.parse_args()
    result = trace(a.case, a.dll, a.sequence, first_draw=a.first_draw, draw_count=a.draw_count, target=a.target, roi=a.roi)
    print(json.dumps({k: v for k, v in result.items() if k != 'draws'}, indent=2))


if __name__ == '__main__':
    main()
