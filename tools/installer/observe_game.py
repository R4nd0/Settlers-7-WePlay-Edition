"""Passive, bounded telemetry for one explicitly selected owned S7 test process.

Only process/thread query and process-memory read rights are used. No input,
game calls, writes, thread suspension, priority or affinity changes. Native
state means counter progress, never an inferred menu/lobby or checksum result.
PresentMon is optional, PID-scoped and input tracking is explicitly disabled.
"""
import argparse
import base64
import csv
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import struct
import subprocess
import time
import uuid

from observe_input_pump import ADAPTERS as INPUT_ADAPTERS, EXE, Module, Process

# Each extra entry requires a map from that build's saved objects, with section
# positions and bytes checked against its DLL (never map another relink).
ADAPTERS = {**INPUT_ADAPTERS,
    'cfb1e3432a7990559bd167a5cdef97e68c539312535f0b846b3c7db3f3f5cb7f':
        (0x3dfd6, 0x3e068, 0x3e06c, 0x3e070),
    # Private affinity-off measurement control; not a released game adapter.
    'd6dfc374b823666b4f97b24165eabefd68d6a9623fe2fd4d216287a00a2348c4':
        (0x3cfd6, 0x3d068, 0x3d06c, 0x3d070),
}
ADAPTER_IMAGES = {key: 0x44000 for key in ADAPTERS}
ADAPTER_IMAGES['d6dfc374b823666b4f97b24165eabefd68d6a9623fe2fd4d216287a00a2348c4'] = 0x43000
BOOTSTRAP_THREAD_RVAS = {
    '0d5fdd965485f0ad7414802c9012a203a71df4ab358422737952667d9308b650': 0x3e064,
    'cfb1e3432a7990559bd167a5cdef97e68c539312535f0b846b3c7db3f3f5cb7f': 0x3e064,
    'd6dfc374b823666b4f97b24165eabefd68d6a9623fe2fd4d216287a00a2348c4': 0x3d064,
}

PRESENTMON_SHA = 'b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af'
REPO = Path(__file__).resolve().parents[2]
MAX_SECONDS = 120
MAX_THREADS = 512


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def bounded_options(pid, seconds, interval):
    if not 1 <= pid <= 0xffffffff:
        raise ValueError('Ein expliziter gueltiger Spiel-PID ist erforderlich.')
    if not math.isfinite(seconds) or not 1 <= seconds <= MAX_SECONDS:
        raise ValueError('Messdauer muss zwischen 1 und 120 Sekunden liegen.')
    if not math.isfinite(interval) or not 0.25 <= interval <= 10:
        raise ValueError('Messabstand muss zwischen 0,25 und 10 Sekunden liegen.')
    return math.ceil(seconds / interval)


def owned_output(path, workspace=REPO):
    work = (Path(workspace) / 'work').resolve(strict=True)
    target = Path(path).resolve()
    if not target.is_relative_to(work) or target == work:
        raise ValueError('Messausgabe muss innerhalb des eigenen work-Ordners liegen.')
    if target.exists():
        raise ValueError('Messausgabe existiert bereits; neuen Ordner angeben.')
    return target


def counter_delta(previous, current):
    return (current - previous) & 0xffffffff


def state_label(previous, current):
    if previous is None or current is None:
        return 'native-state-unavailable'
    change = counter_delta(previous['command_turn'], current['command_turn'])
    if change == 0:
        return 'simulation-idle'
    if change < 0x80000000:
        return 'simulation-advancing'
    return 'simulation-counter-reset'


def presentmon_command(executable, pid, seconds, output, session):
    bounded_options(pid, seconds, 1)
    return [str(executable), '--process_id', str(pid), '--timed', str(math.ceil(seconds)),
            '--terminate_after_timed', '--terminate_on_proc_exit', '--no_console_stats',
            '--no_track_input', '--session_name', session, '--output_file', str(output),
            '--v2_metrics']


def summarize_frames(path, pid):
    """FrameTime is PresentMon's CPU frame interval, not displayed FPS."""
    if not Path(path).is_file():
        return {'status': 'no-frame-csv', 'frames': 0}
    # Bound disk parsing independently of a collector failure or supplied file.
    if Path(path).stat().st_size > 64 * 1024 * 1024:
        return {'status': 'frame-csv-size-limit', 'frames': 0}
    chains = {}
    rejected = 0
    with Path(path).open(newline='', encoding='utf-8-sig') as source:
        reader = csv.DictReader(source)
        if not {'ProcessID', 'FrameTime', 'SwapChainAddress'} <= set(reader.fieldnames or []):
            return {'status': 'unsupported-frame-columns', 'frames': 0}
        for row in reader:
            try:
                if int(row['ProcessID']) != pid:
                    rejected += 1
                    continue
                duration = float(row['FrameTime'])
                # Do not erase a long stall from a capture that can last120s.
                # A frame can also begin before recording, so retain every
                # finite positive interval rather than censoring slow frames.
                if not math.isfinite(duration) or duration <= 0:
                    rejected += 1
                    continue
                chains.setdefault(row['SwapChainAddress'], []).append(duration)
            except (ValueError, TypeError):
                rejected += 1
    values = []
    # Keep independent swapchains separate; no addresses leave the summary.
    for durations in chains.values():
        ordered = sorted(durations)
        mean = statistics.fmean(ordered)
        values.append({'frames': len(ordered), 'mean_frame_ms': round(mean, 3),
                       'median_frame_ms': round(statistics.median(ordered), 3),
                       'p95_frame_ms': round(ordered[math.ceil(len(ordered) * .95)-1], 3),
                       'max_frame_ms': round(ordered[-1], 3),
                       'frames_over_50_ms': sum(x > 50 for x in ordered),
                       'frames_over_1000_ms': sum(x > 1000 for x in ordered),
                       'cpu_frame_rate_hz': round(1000 / mean, 3)})
    return {'status': 'frames-observed' if values else 'no-target-frames',
            'frames': sum(x['frames'] for x in values), 'swapchains': values,
            'rejected_rows': rejected, 'metric': 'cpu-frame-interval-not-displayed-fps'}


def collector_diagnostics(path):
    """Only fixed collector statuses, never copy arbitrary log lines."""
    with Path(path).open('rb') as source:
        data = source.read(65536)
    encoding = 'utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else (
        'utf-16-le' if data[1::2].count(0) > len(data) // 4 else 'utf-8')
    raw = data.decode(encoding, errors='replace').lower()
    return {'recording_started': 'started recording.' in raw,
            'recording_stopped': 'stopped recording.' in raw,
            'privilege_warning': 'requires elevated privilege' in raw,
            'event_overflow_warning': 'overflowed present events detected' in raw,
            'log_truncated': Path(path).stat().st_size > 65536}


class Thread(c.Structure):
    _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('tid', w.DWORD),
                ('owner', w.DWORD), ('base_priority', w.LONG), ('delta_priority', w.LONG),
                ('flags', w.DWORD)]


class GroupAffinity(c.Structure):
    _fields_ = [('mask', c.c_size_t), ('group', w.WORD), ('reserved', w.WORD * 3)]


class Memory(c.Structure):
    _fields_ = [('size', w.DWORD), ('page_faults', w.DWORD)] + [
        (name, c.c_size_t) for name in ('peak_working', 'working', 'peak_paged', 'paged',
                                      'peak_nonpaged', 'nonpaged', 'pagefile',
                                      'peak_pagefile', 'private')]


class Windows:
    def __init__(self):
        if os.name != 'nt':
            raise RuntimeError('Diese Messung erfordert Windows.')
        self.k = c.WinDLL('kernel32', use_last_error=True)
        self.u = c.WinDLL('user32', use_last_error=True)
        self.p = c.WinDLL('psapi', use_last_error=True)
        signatures = {
            'OpenProcess': ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
            'OpenThread': ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
            'CloseHandle': ([w.HANDLE], w.BOOL),
            'CreateToolhelp32Snapshot': ([w.DWORD, w.DWORD], w.HANDLE),
            'Process32FirstW': ([w.HANDLE, c.POINTER(Process)], w.BOOL),
            'Process32NextW': ([w.HANDLE, c.POINTER(Process)], w.BOOL),
            'Module32FirstW': ([w.HANDLE, c.POINTER(Module)], w.BOOL),
            'Module32NextW': ([w.HANDLE, c.POINTER(Module)], w.BOOL),
            'Thread32First': ([w.HANDLE, c.POINTER(Thread)], w.BOOL),
            'Thread32Next': ([w.HANDLE, c.POINTER(Thread)], w.BOOL),
            'QueryFullProcessImageNameW': ([w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)], w.BOOL),
            'ReadProcessMemory': ([w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)], w.BOOL),
            'GetProcessAffinityMask': ([w.HANDLE, c.POINTER(c.c_size_t), c.POINTER(c.c_size_t)], w.BOOL),
            'GetThreadGroupAffinity': ([w.HANDLE, c.POINTER(GroupAffinity)], w.BOOL),
            'GetProcessIdOfThread': ([w.HANDLE], w.DWORD),
            'GetExitCodeProcess': ([w.HANDLE, c.POINTER(w.DWORD)], w.BOOL),
            'GetPriorityClass': ([w.HANDLE], w.DWORD),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.k, name); fn.argtypes = args; fn.restype = result
        for name in ('GetProcessTimes', 'GetThreadTimes'):
            fn = getattr(self.k, name)
            fn.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4
            fn.restype = w.BOOL
        self.p.GetProcessMemoryInfo.argtypes = [w.HANDLE, c.POINTER(Memory), w.DWORD]
        self.p.GetProcessMemoryInfo.restype = w.BOOL
        self.u.GetForegroundWindow.restype = w.HWND
        self.u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]

    def snapshot(self, flags, pid, item_type, first, next_):
        handle = self.k.CreateToolhelp32Snapshot(flags, pid)
        if handle == c.c_void_p(-1).value:
            raise RuntimeError('Begrenzte Windows-Metadaten nicht verfuegbar.')
        try:
            item = item_type(); item.size = c.sizeof(item)
            valid = getattr(self.k, first)(handle, c.byref(item))
            while valid:
                yield item
                item = item_type(); item.size = c.sizeof(item)
                valid = getattr(self.k, next_)(handle, c.byref(item))
            if c.get_last_error() != 18:  # ERROR_NO_MORE_FILES is the only normal end.
                raise RuntimeError('Windows-Metadatenaufzaehlung unvollstaendig.')
        finally:
            self.k.CloseHandle(handle)

    def path(self, handle):
        value = c.create_unicode_buffer(32768); size = w.DWORD(len(value))
        if not self.k.QueryFullProcessImageNameW(handle, 0, value, c.byref(size)):
            raise RuntimeError('Prozesspfad nicht lesbar.')
        return Path(value.value).resolve(strict=True)

    def times(self, handle, thread=False):
        values = [w.FILETIME() for _ in range(4)]
        fn = self.k.GetThreadTimes if thread else self.k.GetProcessTimes
        if not fn(handle, *(c.byref(value) for value in values)):
            raise RuntimeError('Prozess-/Threadzeit nicht lesbar.')
        raw = [(value.dwHighDateTime << 32) | value.dwLowDateTime for value in values]
        return raw[0], (raw[2] + raw[3]) / 1e7


class OwnedGame:
    def __init__(self, api, root, pid):
        self.api = api; self.handle = None
        self.root = Path(root).resolve(strict=True); self.pid = pid
        self.exe = (self.root / 'Base/Bin/Settlers7R.exe').resolve(strict=True)
        self.adapter = self.exe.with_name('uplay_r1_loader.dll').resolve(strict=True)
        if not self.exe.is_relative_to(self.root) or not self.adapter.is_relative_to(self.root):
            raise RuntimeError('Spielmodule verlassen den angegebenen Testordner.')
        if digest(self.exe) != EXE:
            raise RuntimeError('Unbekannte History-EXE.')
        self.adapter_hash = digest(self.adapter)
        if self.adapter_hash not in ADAPTERS:
            raise RuntimeError('Adapter ohne validierte Beobachter-Zuordnung.')
        entries = [x for x in api.snapshot(2, 0, Process, 'Process32FirstW', 'Process32NextW') if x.pid == pid]
        if len(entries) != 1 or entries[0].name.lower() != 'settlers7r.exe':
            raise RuntimeError('Expliziter Spielprozess fehlt.')
        parent = api.k.OpenProcess(0x1000, False, entries[0].parent)
        if not parent:
            raise RuntimeError('Normaler Teststarter nicht verifizierbar.')
        try:
            expected_parent = (self.root / 'Runtime/python/python.exe').resolve(strict=True)
            if not expected_parent.is_relative_to(self.root) or api.path(parent) != expected_parent:
                raise RuntimeError('Spiel muss vom eigenen normalen Teststarter stammen.')
            parent_created, _ = api.times(parent)
        finally:
            api.k.CloseHandle(parent)
        self.handle = api.k.OpenProcess(0x1010, False, pid)  # QUERY_LIMITED_INFORMATION | VM_READ
        if not self.handle:
            raise RuntimeError('Lesender Zugriff auf den Testprozess fehlt.')
        try:
            if api.path(self.handle) != self.exe:
                raise RuntimeError('Prozesspfad stimmt nicht mit der Testkopie ueberein.')
            self.created, _ = api.times(self.handle)
            if parent_created > self.created:
                raise RuntimeError('Elternprozess wurde zwischenzeitlich ersetzt.')
            modules = list(api.snapshot(0x18, pid, Module, 'Module32FirstW', 'Module32NextW'))
            self.base = self.module_base(modules, self.exe, 0x10e7000)
            self.adapter_base = self.module_base(modules, self.adapter, ADAPTER_IMAGES[self.adapter_hash])
        except BaseException:
            self.close(); raise

    @staticmethod
    def module_base(modules, path, size):
        # Windows may report unavailable WoW64 aliases for unrelated GPU DLLs.
        # Only inspect the expected module; no driver/account paths are needed.
        unique = {}
        for item in modules:
            if Path(item.path).name.casefold() != path.name.casefold():
                continue
            try:
                resolved = Path(item.path).resolve(strict=True)
            except OSError:
                raise RuntimeError('Pfad des erwarteten Spielmoduls ist nicht lesbar.') from None
            # SNAPMODULE|SNAPMODULE32 may repeat the SAME WoW64 image in both
            # views. Coalesce only an identical address/size/canonical-path.
            unique[(item.base, item.image_size, resolved)] = item
        found = list(unique.values())
        if len(found) != 1 or found[0].image_size != size or not found[0].base:
            raise RuntimeError('Bekanntes geladenes Spielmodul oder seine Groesse fehlt.')
        matches = Path(found[0].path).resolve(strict=True) == path
        if not matches:
            raise RuntimeError('Geladenes Spielmodul liegt nicht im eigenen Testordner.')
        return found[0].base

    def close(self):
        if self.handle:
            self.api.k.CloseHandle(self.handle); self.handle = None

    def read(self, address, size):
        if not 1 <= size <= 4 or not 0x10000 <= address <= 0xffffffff-size:
            raise RuntimeError('Ungueltiger begrenzter x86-Zustandszeiger.')
        value = c.create_string_buffer(size); got = c.c_size_t()
        if not self.api.k.ReadProcessMemory(self.handle, address, value, size, c.byref(got)) or got.value != size:
            raise RuntimeError('Begrenzter nativer Zustand ist nicht mehr lesbar.')
        return value.raw

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]

    def at(self, address):
        return self.base + address - 0x400000

    def typed(self, address, table):
        if self.u32(address) != self.at(table):
            raise RuntimeError('Nativer Zustand hat keinen validierten Typ.')
        return address

    def native(self):
        # Same narrowly typed counters as observe_input_pump, no descriptors/IDs.
        network = self.u32(self.at(0x1379e14))
        owner = self.typed(self.u32(network + 0xa18), 0x10919bc)
        self.typed(owner + 0x50, 0x109c9d8)
        master = self.typed(network + 0x5c, 0x1091e3c)
        core = self.typed(self.u32(network + 0x2c), 0x10940a0)
        if self.u32(master + 0x4c0) != core:
            raise RuntimeError('Native Kernzuordnung ungueltig.')
        impl = self.typed(self.u32(core + 0x20), 0x1099548)
        scheduler = self.typed(self.u32(impl + 4), 0x109959c)
        main = self.typed(self.u32(self.at(0x13a5210)), 0x113f2c0)
        engine = self.typed(self.u32(main + 0x18), 0x113f424)
        rvas = ADAPTERS[self.adapter_hash]
        state = {'command_turn': self.u32(engine + 0x20),
                'command_turn_offset': self.u32(engine + 0x24),
                'command_offset_target': self.u32(engine + 0x28),
                'desired_turn_offset': self.read(engine + 0x60, 1)[0],
                'master_started': self.read(master + 0x4c4, 1)[0],
                'core_enabled': self.read(master + 0x4c7, 1)[0],
                'master_state': self.u32(master + 0x400),
                'natural_updates_seen': self.read(self.adapter_base + rvas[0], 1)[0],
                'core_dispatches': self.u32(scheduler + 0x78),
                **{name: self.u32(self.adapter_base + rva) for name, rva in zip(
                    ('owner_updates', 'manager_updates', 'echo_updates'), rvas[1:])}}
        if self.adapter_hash in BOOTSTRAP_THREAD_RVAS:
            state['bootstrap_os_thread_id'] = self.u32(self.adapter_base + BOOTSTRAP_THREAD_RVAS[self.adapter_hash])
        return state

    def sample(self):
        api = self.api
        code = w.DWORD()
        if not api.k.GetExitCodeProcess(self.handle, c.byref(code)) or code.value != 259:
            raise RuntimeError('Testspiel wurde beendet; Messung endet.')
        if api.path(self.handle) != self.exe or api.times(self.handle)[0] != self.created:
            raise RuntimeError('Testprozess wurde ersetzt.')
        mask = c.c_size_t(); system = c.c_size_t()
        if not api.k.GetProcessAffinityMask(self.handle, c.byref(mask), c.byref(system)):
            raise RuntimeError('Prozesszuordnung nicht lesbar.')
        mem = Memory(); mem.size = c.sizeof(mem)
        if not api.p.GetProcessMemoryInfo(self.handle, c.byref(mem), mem.size):
            raise RuntimeError('Prozessspeicherstatistik nicht lesbar.')
        foreground = w.DWORD()
        api.u.GetWindowThreadProcessId(api.u.GetForegroundWindow(), c.byref(foreground))
        threads = []; inaccessible = 0
        entries = [x for x in api.snapshot(4, 0, Thread, 'Thread32First', 'Thread32Next') if x.owner == self.pid]
        if len(entries) > MAX_THREADS:
            raise RuntimeError('Threadanzahl ueber Messgrenze.')
        for item in entries:
            handle = api.k.OpenThread(0x40, False, item.tid)  # THREAD_QUERY_INFORMATION
            if not handle:
                inaccessible += 1; continue
            try:
                if api.k.GetProcessIdOfThread(handle) != self.pid:
                    inaccessible += 1; continue  # Snapshot TID may have been recycled.
                created, cpu = api.times(handle, thread=True)
                group = GroupAffinity()
                if not api.k.GetThreadGroupAffinity(handle, c.byref(group)):
                    inaccessible += 1; continue
                threads.append({'tid': item.tid, 'created_100ns': created, 'cpu_seconds': cpu,
                                'group': group.group, 'affinity': hex(group.mask)})
            except RuntimeError:
                inaccessible += 1
            finally:
                api.k.CloseHandle(handle)
        try:
            native = self.native()
        except RuntimeError:
            native = None  # Startup/shutdown and probe-off are not guessed UI states.
        return {'cpu_seconds': api.times(self.handle)[1], 'working_bytes': mem.working,
                'private_bytes': mem.private, 'process_affinity': hex(mask.value),
                'priority_class': api.k.GetPriorityClass(self.handle),
                'foreground': foreground.value == self.pid, 'thread_count': len(entries),
                'inaccessible_threads': inaccessible, 'threads': threads, 'native': native}


def interval_record(previous, current, seconds):
    result = dict(current)
    result['elapsed_seconds'] = round(seconds, 6)
    result['state'] = state_label(previous['native'], current['native'])
    result['cpu_core_equivalents'] = round(max(0, current['cpu_seconds']-previous['cpu_seconds']) / seconds, 4)
    before = {(x['tid'], x['created_100ns']): x for x in previous['threads']}
    result['threads'] = [{**x, 'cpu_core_equivalents': round(max(0, x['cpu_seconds'] -
        before[(x['tid'], x['created_100ns'])]['cpu_seconds']) / seconds, 4)
        if (x['tid'], x['created_100ns']) in before else None} for x in current['threads']]
    if previous['native'] is not None and current['native'] is not None:
        result['native_counter_deltas'] = {name: counter_delta(previous['native'][name], current['native'][name])
            for name in ('core_dispatches', 'owner_updates', 'manager_updates', 'echo_updates')}
    return result


def validate_presentmon(path):
    path = Path(path).resolve(strict=True)
    if digest(path) != PRESENTMON_SHA:
        raise RuntimeError('PresentMon ist nicht die festgelegte Version 2.6.0.')
    # Hash pin is primary. Independently require a currently valid signature.
    quoted = "'" + str(path).replace("'", "''") + "'"
    # Intel's pinned release has no PE ProductVersion/FileVersion resources.
    # PowerShell serializes SignatureStatus.Valid as JSON number 0, not 'Valid'.
    script = f"$p={quoted}; $s=Get-AuthenticodeSignature -LiteralPath $p; [pscustomobject]@{{Status=[int]$s.Status; StatusText=$s.Status.ToString(); SignerSubject=$s.SignerCertificate.Subject; ProductVersion=(Get-Item -LiteralPath $p).VersionInfo.ProductVersion}} | ConvertTo-Json -Compress"
    encoded = base64.b64encode(script.encode('utf-16-le')).decode('ascii')
    shell = shutil.which('pwsh.exe') or shutil.which('powershell.exe')
    if not shell:
        raise RuntimeError('PowerShell fuer die Signaturpruefung fehlt.')
    check = subprocess.run([shell, '-NoProfile', '-NonInteractive', '-EncodedCommand', encoded],
                           capture_output=True, timeout=20, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if check.returncode:
        raise RuntimeError('PresentMon-Signatur/Versionspruefung fehlgeschlagen.')
    validate_signature_json(check.stdout)
    banner = subprocess.run([str(path), '--help'], capture_output=True, timeout=10,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    # --help intentionally exits 1 and this Intel build writes help to stderr.
    first = (banner.stdout or banner.stderr).decode('utf-8', errors='replace').strip().splitlines()
    if banner.returncode not in (0, 1) or not first or first[0].strip() != 'PresentMon 2.6.0':
        raise RuntimeError('PresentMon meldet nicht die festgelegte Version 2.6.0.')
    return path


def validate_signature_json(raw):
    try:
        value = json.loads(raw)
        valid = (type(value.get('Status')) is int and value['Status'] == 0 and
                 value.get('StatusText') == 'Valid' and
                 isinstance(value.get('SignerSubject'), str) and
                 value['SignerSubject'].split(', ')[0] == 'CN=Intel Corporation')
    except (ValueError, TypeError, AttributeError):
        valid = False
    if not valid:
        raise RuntimeError('PresentMon hat keine gueltige Intel-Authenticode-Signatur.')


def observe(root, pid, output, seconds=30, interval=1, presentmon=None, quiet=False):
    count = bounded_options(pid, seconds, interval)
    output = owned_output(output)
    collector = validate_presentmon(presentmon) if presentmon else None
    game = OwnedGame(Windows(), root, pid)
    capture = None; log = None
    try:
        output.mkdir(parents=True)
        if collector:
            log = (output / 'presentmon.log').open('wb')
            capture = subprocess.Popen(presentmon_command(collector, pid, seconds,
                output / 'frames.csv', 'S7Passive-' + uuid.uuid4().hex), stdout=log, stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        with (output / 'samples.jsonl').open('x', encoding='utf-8') as destination:
            def emit(value):
                destination.write(json.dumps(value, separators=(',', ':')) + '\n'); destination.flush()
                if not quiet or value['event'] == 'passive-frame-summary':
                    print(json.dumps(value, separators=(',', ':')), flush=True)
            previous = game.sample(); start = prior_time = time.monotonic()
            emit({'event': 'passive-observation-start', 'pid': pid, 'adapter_sha256': game.adapter_hash,
                  'max_seconds': seconds, 'sample': previous})
            for index in range(count):
                deadline = start + min((index + 1) * interval, seconds)
                time.sleep(max(0, deadline - time.monotonic()))
                current = game.sample(); now = time.monotonic()
                emit({'event': 'passive-observation-sample',
                      **interval_record(previous, current, now-prior_time)})
                previous = current; prior_time = now
                if now - start >= seconds:
                    break
            # Detect on-disk replacement; the loaded handle remains pinned throughout.
            if digest(game.exe) != EXE or digest(game.adapter) != game.adapter_hash:
                raise RuntimeError('Moduldateien wurden waehrend der Messung ersetzt.')
            if capture:
                try:
                    capture.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    capture.terminate(); capture.wait(timeout=5)  # Only our own collector, never the game.
                log.close(); log = None
                frames = summarize_frames(output / 'frames.csv', pid)
                emit({'event': 'passive-frame-summary', 'collector_exit_code': capture.returncode,
                      **collector_diagnostics(output / 'presentmon.log'), **frames})
    finally:
        if capture and capture.poll() is None:
            capture.terminate(); capture.wait(timeout=5)
        if log:
            log.close()
        game.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--output', required=True)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--interval', type=float, default=1)
    parser.add_argument('--presentmon', help='Optional: exact signed PresentMon 2.6.0 executable.')
    parser.add_argument('--quiet', action='store_true', help='Write detailed samples only to the owned output folder.')
    args = parser.parse_args()
    try:
        observe(args.bundle, args.pid, args.output, args.seconds, args.interval, args.presentmon, args.quiet)
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        reason = str(error) if isinstance(error, (RuntimeError, ValueError)) else 'Begrenzte Windows-/Dateidiagnose nicht verfuegbar.'
        print(json.dumps({'event': 'passive-observation-stopped', 'reason': reason}), flush=True)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
