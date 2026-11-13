"""Read-only memory sentinel for a Popen child; never kills or alters a game.

Pressure warnings cannot guarantee that the next allocation will succeed.
Only aggregates are stored: no game memory, paths, identities or packet data.
"""
import ctypes as C
from ctypes import wintypes as W
from functools import lru_cache
import hashlib
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
SCANNER_SHA256 = 'cc1dd73539404a7d28557f5ae9f2fecd8dca0f35eee62fe32982c3785c18844c'


class ScanResult(C.Structure):
    _fields_ = [(name, W.DWORD) for name in ('size','version','status','error','regions','elapsed_us')] + [
        ('free_bytes', C.c_uint64), ('largest_bytes', C.c_uint64)]


def verify_scanner(path):
    path = Path(path)
    if path.is_symlink() or path.stat().st_size > 1024*1024 or hashlib.sha256(path.read_bytes()).hexdigest() != SCANNER_SHA256:
        raise ValueError('Ungepruefte Speichermessbibliothek.')
    return path


@lru_cache(maxsize=1)
def scanner_api():
    path = scanner_path()
    kernel = C.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [W.LPCWSTR,W.DWORD,W.DWORD,C.c_void_p,W.DWORD,W.DWORD,W.HANDLE]
    kernel.CreateFileW.restype = W.HANDLE
    kernel.CloseHandle.argtypes = [W.HANDLE]
    kernel.CloseHandle.restype = W.BOOL
    # Keep the exact verified file locked against writes/replacement for the
    # lifetime of the loaded library. It imports only Windows system libraries.
    handle = kernel.CreateFileW(str(path),0x80000000,1,None,3,0x00200000,None)
    if handle == C.c_void_p(-1).value:
        raise C.WinError(C.get_last_error())
    try:
        verify_scanner(path)
        library = C.WinDLL(str(path), use_last_error=True, winmode=0x800)
        api = library.S7ScanAddressSpace
        api.argtypes = [W.HANDLE,C.POINTER(ScanResult),W.DWORD]
        api.restype = W.DWORD
        api._retained = (library, kernel, handle)
        return api
    except BaseException:
        kernel.CloseHandle(handle)
        raise


def scanner_path():
    script = Path(__file__).resolve()
    adjacent = script.with_name('memory_scan.dll')
    if adjacent.exists():return adjacent
    repo = script.parents[2]
    # Source debugger/graphics tools also use this monitor. Only an actual
    # checkout can resolve the exact qualified build in its private cache.
    # Installed/portable packages must contain their own adjacent DLL.
    if script == repo/'tools/d3d11/memory_guard.py' and (repo/'.git').exists():
        return repo/'work/memory-scan'/SCANNER_SHA256/'memory_scan.dll'
    return adjacent


class Counters(C.Structure):
    _fields_ = [('cb',W.DWORD),('faults',W.DWORD)] + [
        (name,C.c_size_t) for name in ('peak_working','working','peak_paged','paged',
            'peak_nonpaged','nonpaged','pagefile','peak_pagefile','private')]


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
    def __init__(self, message, *, code='memory_low'):
        super().__init__(message)
        self.code = code


class AddressScanTimeout(OSError):
    """An incomplete scan is discarded, never published as a measurement."""


def require_host_headroom(sample=None):
    """Conservative budget for the isolated graphics experiments only."""
    sample = host_memory() if sample is None else sample
    if min(sample['host_physical_free'],sample['host_commit_free']) < 4096*MIB:
        raise HostMemoryError('Zu wenig freier Systemspeicher fuer den Grafikversuch; '
                           'mindestens 4 GiB freier RAM und Commit-Spielraum erforderlich.')


def require_game_headroom(sample=None):
    """Production preflight: available physical pages are not a memory quota.

    Windows can page and trim working sets when the game requests memory. Block
    only the critical system-commit level already used by the sentinel. Leave
    physical-memory pressure to warnings and the running process sentinel. This does
    not allocate/reserve memory or predict whether future allocations succeed.
    """
    sample = host_memory() if sample is None else sample
    for key in ('host_physical_free', 'host_commit_free'):
        if type(sample.get(key)) is not int or sample[key] < 0:
            raise HostMemoryError('Systemspeicherzaehler nicht verfuegbar.', code='memory_status_failed')
    if sample['host_commit_free'] <= 1024*MIB:
        raise HostMemoryError('Windows meldet akuten Speicherdruck: hoechstens 1 GiB '
                              'zusaetzlicher Commit-Spielraum ist verfuegbar.',
                              code='memory_commit_low')
    return sample


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


def allocation_pressure(sample):
    """Allocation headroom for owned tests; physical availability stays telemetry.

    Windows may trim/page working sets. The finite system commit budget and the
    game's 32-bit virtual address space remain relevant allocation constraints.
    """
    commit = sample.get('host_commit_free',LIMIT)
    commit_level = 2 if commit <= 1024*MIB else 1 if commit <= 2048*MIB else 0
    return max(process_pressure(sample),commit_level)


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
            try:
                address_sample = self.address_space()
            except AddressScanTimeout:
                # One scheduling delay must not permanently disable monitoring.
                # Restart from address zero with the same 50 ms / 20,000-region
                # limits. A second timeout (or any other error) remains fatal.
                address_sample = self.address_space()
            self.address_sample = address_sample
            self.last_scan = time.monotonic()
        return {'private':counters.private,**self.address_sample,**host_memory()}

    def address_space(self):
        # The entire bounded scan runs inside one native call. Thousands of
        # individual ctypes calls previously yielded the GIL to other launcher
        # threads between queries and exhausted both 50 ms attempts under load.
        result = ScanResult()
        if scanner_api()(self.handle,C.byref(result),C.sizeof(result)):
            raise OSError('Adressraumpruefung: Aufruf fehlgeschlagen.')
        if result.size != C.sizeof(result) or result.version != 1 or result.status > 5:
            raise OSError('Adressraumpruefung: unbekannte Messschnittstelle.')
        if result.status == 4:
            raise AddressScanTimeout('Adressraumpruefung hat ihr Zeitbudget ueberschritten.')
        if result.status:
            raise OSError(result.error,'Adressraumpruefung unvollstaendig (Status '+str(result.status)+').')
        if not 1 <= result.regions <= 20000 or result.elapsed_us > 50000 or not 0 <= result.largest_bytes <= result.free_bytes <= LIMIT:
            raise OSError('Adressraumpruefung: ungueltige Summenwerte.')
        self.last_scan_ms = result.elapsed_us/1000
        self.last_scan_regions = result.regions
        return {'free':result.free_bytes,'largest':result.largest_bytes}


def notify(level):
    text = ('Der verfuegbare Speicher wird knapp. Bitte jetzt speichern und das Spiel neu starten. '
            'Der naechste Start verwendet besonders sparsame Grafikwerte. '
            'Das Spiel wird nicht automatisch beendet; ein Speicherfehler ist weiterhin moeglich.')
    if level == 3:
        text = ('Die Speicherueberwachung ist ausgefallen. Bitte speichern und neu starten. '
                'Der naechste Start verwendet besonders sparsame Grafikwerte.')
    C.WinDLL('user32').MessageBoxW(None,text,'Siedler 7: Speicherwarnung',0x30 | 0x10000)


class Sentinel:
    def __init__(self, child, path, probe, *, warn=notify, sticky=False, pressure_policy=None):
        self.child, self.path, self.probe, self.warn = child, path, probe, warn
        self.pressure_policy = pressure_policy
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
        scan_ms = getattr(self.probe,'last_scan_ms',None)
        if type(scan_ms) in (int,float):
            self.summary['scanner'] = 'native-v1'
            self.summary['peak_scan_ms'] = max(self.summary.get('peak_scan_ms',0),scan_ms)
            self.summary['peak_scan_regions'] = max(self.summary.get('peak_scan_regions',0),self.probe.last_scan_regions)
        self.observe(sample)

    def observe(self, sample):
        level = pressure(sample) if self.pressure_policy is None else self.pressure_policy(sample)
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
        stage = 'poll'
        try:
            while not self.stop.wait(POLL_SECONDS) and self.child.poll() is None:
                try:
                    stage = 'measure'
                    self.measure()
                except OSError:
                    if self.child.poll() is None:
                        raise
                if time.monotonic()-self.last_save >= SAVE_SECONDS:
                    stage = 'persist'
                    self.persist()
                stage = 'poll'
        except Exception as error:
            self.summary['monitor_failed'] = self.summary['recovery_required'] = True
            # Bounded diagnostics only; never persist exception messages/paths.
            self.summary['failure_stage'] = stage
            self.summary['failure_kind'] = ('scan_timeout' if isinstance(error,AddressScanTimeout)
                                            else 'os_error' if isinstance(error,OSError) else 'internal_error')
            code = getattr(error,'winerror',None) or getattr(error,'errno',None)
            if type(code) is int:self.summary['failure_code'] = code
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
