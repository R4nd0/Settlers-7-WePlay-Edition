"""Read-only memory sentinel for a Popen child; never kills or alters a game.

Pressure warnings cannot guarantee that the next allocation will succeed.
Only aggregates are stored: no game memory, paths, identities or packet data.
"""
import ctypes as C
from ctypes import wintypes as W
from functools import lru_cache
import json
import os
from pathlib import Path
import tempfile
import threading
import time

MIB = 1024 * 1024
LIMIT = 1 << 32
POLL_SECONDS = 2.0
ADDRESS_SCAN_SECONDS = 10.0
SAVE_SECONDS = 30.0


class Counters(C.Structure):
    _fields_ = [('cb',W.DWORD),('faults',W.DWORD)] + [
        (name,C.c_size_t) for name in ('peak_working','working','peak_paged','paged',
            'peak_nonpaged','nonpaged','pagefile','peak_pagefile','private')]


class Region(C.Structure):
    _fields_ = [('base',C.c_void_p),('allocation',C.c_void_p),('allocation_protect',W.DWORD),
                ('partition',W.WORD),('size',C.c_size_t),('state',W.DWORD),
                ('protect',W.DWORD),('kind',W.DWORD)]


class HostCounters(C.Structure):
    _fields_ = [('cb',W.DWORD)] + [(name,C.c_size_t) for name in
        ('commit_total','commit_limit','commit_peak','physical_total','physical_available',
         'system_cache','kernel_total','kernel_paged','kernel_nonpaged','page_size')] + [
        (name,W.DWORD) for name in ('handle_count','process_count','thread_count')]


@lru_cache(maxsize=1)
def host_api():
    api = C.WinDLL('psapi',use_last_error=True).GetPerformanceInfo
    api.argtypes = [C.POINTER(HostCounters),W.DWORD]
    api.restype = W.BOOL
    return api


def host_memory():
    value = HostCounters(); value.cb = C.sizeof(value)
    if not host_api()(C.byref(value),value.cb) or not value.page_size:
        raise OSError('Systemspeicherzaehler nicht verfuegbar.')
    if value.commit_total > value.commit_limit or value.physical_available > value.physical_total:
        raise OSError('Systemspeicherzaehler widerspruechlich.')
    return {'host_physical_free':value.physical_available*value.page_size,
            'host_commit_free':(value.commit_limit-value.commit_total)*value.page_size}


class HostMemoryError(RuntimeError):
    pass


def require_host_headroom(sample=None):
    sample = host_memory() if sample is None else sample
    if min(sample['host_physical_free'],sample['host_commit_free']) < 4096*MIB:
        raise HostMemoryError('Zu wenig freier Systemspeicher fuer den Grafikversuch; '
                           'mindestens 4 GiB freier RAM und Commit-Spielraum erforderlich.')


def process_pressure(sample):
    private, free, largest = (sample[k] for k in ('private', 'free', 'largest'))
    if private >= 3328*MIB or free <= 256*MIB or largest <= 16*MIB:
        return 2
    if private >= 2816*MIB or free <= 768*MIB or largest <= 64*MIB:
        return 1
    return 0


def host_pressure(sample):
    physical = sample.get('host_physical_free',LIMIT)
    commit = sample.get('host_commit_free',LIMIT)
    if physical <= 768*MIB or commit <= 1024*MIB:
        return 2
    if physical <= 1536*MIB or commit <= 2048*MIB:
        return 1
    return 0


def pressure(sample):
    return max(process_pressure(sample),host_pressure(sample))


def save(path, value):
    # Caller validates the private LocalData path before starting the sentinel.
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write((json.dumps(value, indent=2)+'\n').encode('ascii'))
        stream.flush(); os.fsync(stream.fileno())
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def recovery_required(path):
    if not path.exists():
        return False
    if path.stat().st_size > 4096:
        raise ValueError('Unbekannter Speicherbericht.')
    value = json.loads(path.read_text('ascii'))
    if (not isinstance(value, dict) or value.get('format') != 's7-memory-summary-v1'
            or type(value.get('recovery_required')) is not bool):
        raise ValueError('Unbekannter Speicherbericht.')
    return value['recovery_required']


class Probe:
    """Keep the child's original process HANDLE, never reopen a numeric PID."""
    def __init__(self, child, executable):
        if os.name != 'nt' or C.sizeof(C.c_void_p) != 8:
            raise RuntimeError('Speicherpruefung benoetigt den 64-Bit-Starthelfer.')
        self.child = child
        self.address_sample = None
        self.last_scan = float('-inf')
        self.handle = W.HANDLE(int(child._handle))
        self.kernel = C.WinDLL('kernel32', use_last_error=True)
        self.psapi = C.WinDLL('psapi', use_last_error=True)
        self.kernel.QueryFullProcessImageNameW.argtypes = [W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)]
        self.kernel.QueryFullProcessImageNameW.restype = W.BOOL
        self.kernel.IsWow64Process.argtypes = [W.HANDLE,C.POINTER(W.BOOL)]
        self.kernel.IsWow64Process.restype = W.BOOL
        self.kernel.VirtualQueryEx.argtypes = [W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t]
        self.kernel.VirtualQueryEx.restype = C.c_size_t
        self.psapi.GetProcessMemoryInfo.argtypes = [W.HANDLE,C.c_void_p,W.DWORD]
        self.psapi.GetProcessMemoryInfo.restype = W.BOOL
        name = C.create_unicode_buffer(32768)
        length = W.DWORD(len(name))
        wow = W.BOOL()
        if (not self.kernel.QueryFullProcessImageNameW(self.handle,0,name,C.byref(length))
                or Path(name.value).resolve() != Path(executable).resolve()
                or not self.kernel.IsWow64Process(self.handle,C.byref(wow)) or not wow.value):
            raise RuntimeError('Speicherpruefung konnte das eigene x86-Spiel nicht bestaetigen.')

    def sample(self):
        counters = Counters(); counters.cb = C.sizeof(counters)
        if not self.psapi.GetProcessMemoryInfo(self.handle,C.byref(counters),counters.cb):
            raise OSError('Speicherzaehler nicht verfuegbar.')
        if self.address_sample is None or time.monotonic()-self.last_scan >= ADDRESS_SCAN_SECONDS:
            self.address_sample = self.address_space()
            self.last_scan = time.monotonic()
        return {'private':counters.private,**self.address_sample,**host_memory()}

    def address_space(self):
        deadline = time.monotonic()+0.05
        address = free = largest = 0
        for _ in range(20000):
            if address >= LIMIT:
                break
            if time.monotonic() > deadline:
                raise OSError('Adressraumpruefung hat ihr Zeitbudget ueberschritten.')
            region = Region()
            if self.kernel.VirtualQueryEx(self.handle,C.c_void_p(address),C.byref(region),C.sizeof(region)) != C.sizeof(region):
                raise OSError('Adressraumpruefung unvollstaendig.')
            end = min(LIMIT, (region.base or 0) + region.size)
            if end <= address:
                raise OSError('Ungueltiger Adressbereich.')
            if region.state == 0x10000:  # MEM_FREE, includes neither reserved nor committed pages.
                free += end-address; largest = max(largest,end-address)
            address = end
        else:
            raise OSError('Adressraumpruefung nicht abgeschlossen.')
        return {'free':free,'largest':largest}


def notify(level):
    text = ('Der verfuegbare Speicher wird knapp. Bitte jetzt speichern und das Spiel neu starten. '
            'Der naechste Start verwendet besonders sparsame Grafikwerte. '
            'Das Spiel wird nicht automatisch beendet; ein Speicherfehler ist weiterhin moeglich.')
    if level == 3:
        text = ('Die Speicherueberwachung ist ausgefallen. Bitte speichern und neu starten. '
                'Der naechste Start verwendet besonders sparsame Grafikwerte.')
    C.WinDLL('user32').MessageBoxW(None,text,'Siedler 7: Speicherwarnung',0x30 | 0x10000)


class Sentinel:
    def __init__(self, child, path, probe, *, warn=notify, sticky=False):
        self.child, self.path, self.probe, self.warn = child, path, probe, warn
        self.stop = threading.Event()
        self.summary = {'format':'s7-memory-summary-v1','recovery_required':sticky,
                        'max_pressure':0,'samples':0,'peak_private_mib':0,
                        'min_free_mib':4096,'min_largest_mib':4096,'monitor_failed':False,
                        'peak_probe_ms':0,'min_host_physical_free_mib':None,
                        'min_host_commit_free_mib':None}
        self.streak = 0
        self.warned = False
        self.thread = None
        self.last_save = float('-inf')

    def persist(self):
        save(self.path,self.summary)
        self.last_save = time.monotonic()

    def measure(self):
        started = time.perf_counter()
        sample = self.probe.sample()
        elapsed = round((time.perf_counter()-started)*1000,3)
        self.summary['peak_probe_ms'] = max(self.summary['peak_probe_ms'],elapsed)
        self.observe(sample)

    def observe(self, sample):
        level = pressure(sample)
        self.summary['samples'] += 1
        self.summary['peak_private_mib'] = max(self.summary['peak_private_mib'],sample['private']//MIB)
        for target, key in [('min_free_mib','free'),('min_largest_mib','largest')]:
            self.summary[target] = min(self.summary[target],sample[key]//MIB)
        for key in ('host_physical_free','host_commit_free'):
            if key in sample:
                target = 'min_'+key+'_mib'
                previous = self.summary[target]
                self.summary[target] = sample[key]//MIB if previous is None else min(previous,sample[key]//MIB)
        self.summary['max_pressure'] = max(self.summary['max_pressure'],level)
        self.streak = self.streak+1 if level else 0
        if self.streak >= 3 and not self.warned:
            self.summary['recovery_required'] = True
            self.persist()  # Persist once before displaying a warning.
            self.warned = True
            threading.Thread(target=self.warn,args=(level,),daemon=True).start()

    def run(self):
        try:
            while not self.stop.wait(POLL_SECONDS) and self.child.poll() is None:
                try:
                    self.measure()
                except OSError:
                    if self.child.poll() is None:
                        raise
                if time.monotonic()-self.last_save >= SAVE_SECONDS:
                    self.persist()
        except Exception:
            self.summary['monitor_failed'] = self.summary['recovery_required'] = True
            try:
                self.persist()
            finally:
                if not self.warned:
                    threading.Thread(target=self.warn,args=(3,),daemon=True).start()

    def start(self):
        # Check functionality before reporting a protected start.
        self.measure()
        self.persist()
        self.thread = threading.Thread(target=self.run,daemon=True)
        self.thread.start()

    def close(self):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=5)
            if self.thread.is_alive():
                raise RuntimeError('Speicherpruefung wurde nicht rechtzeitig beendet.')
        self.persist()
