"""Exact River input capture and explicit On12-only finite-product correction."""
from contextlib import ExitStack
import json
from pathlib import Path
import struct
from . import capture_bootstrap as loader,recover

ROOT=Path(__file__).resolve().parents[2]
DLL='WePlayWaterOn12Probe.dll'
SOURCES=('water_on12_probe.cpp','water_on12_probe.def','water_on12_identity.h','water_on12_pow_cap.h','water_on12_shader_cache.h','water_on12_constants.h','water_on12_shader_cache_fixture.cpp','water_on12_probe_gpu.cpp')
BINARIES=(DLL,'water_on12_probe_fixture.exe','water_on12_shader_cache_fixture.exe','water_on12_probe_gpu.exe')


def verify_build(directory):
    directory=recover.lab(directory)
    raw,_=recover._read(directory/'water-probe-build.json',16384)
    value=json.loads(raw.decode('utf-8-sig'))
    if (not isinstance(value,dict) or set(value)!={'format','config_bytes','supports_pow_cap','build_sha256','files'}
            or value['format']!='s7-water-on12-probe-build-v2' or value['config_bytes']!=1036 or value['supports_pow_cap']is not True
            or set(value.get('files',{}))!={*SOURCES,*BINARIES}
            or value['build_sha256']!=loader.digest(ROOT/'native/water_on12_probe_build.ps1')):
        raise ValueError('Stale or unknown exact-water observer build.')
    for name,pin in value['files'].items():
        if loader.digest(directory/name)!=pin or (name in SOURCES and loader.digest(ROOT/'native'/name)!=pin):
            raise ValueError('Water observer source or binary changed; rebuild.')
    return directory/DLL,value


def configuration(report,*,pow_cap=False,benchmark=False):
    if type(pow_cap)is not bool or type(benchmark)is not bool or (benchmark and not pow_cap):raise ValueError('POW cap/benchmark require explicit booleans.')
    report=recover.lab(report);text=str(report);raw=text.encode('utf-16le')
    if report.name!='water-probe.private.json' or '\0' in text or len(raw)>1022 or report.exists():
        raise ValueError('Use a fresh exact-water report in the explicit private case.')
    return struct.pack('<3I',1036,1,3 if benchmark else int(pow_cap))+raw.ljust(1024,b'\0')


class Probe:
    def __init__(self,build,root,output,*,pow_cap=False,benchmark=False):
        from . import game
        self.root,self.output=recover.lab(root),recover.lab(output)
        _,self.executable,_=game.platform.load(self.root)
        if loader.digest(self.executable)!=loader.UBI:
            raise ValueError('Exact-water observer currently supports only the original Ubisoft laboratory profile.')
        self.dll,self.build=verify_build(build)
        self.config=configuration(self.output/'water-probe.private.json',pow_cap=pow_cap,benchmark=benchmark);self.pow_cap=pow_cap;self.benchmark=benchmark
        self.leases=ExitStack();self.prepared=False

    def prepare(self):
        from .load_hook import file_lease
        from . import dxwrapper
        try:
            for path,pin in ((self.dll,self.build['files'][DLL]),
                             (self.executable.with_name('d3d9.dll'),dxwrapper.STUB_SHA256),
                             (self.executable.with_name('dxwrapper.dll'),dxwrapper.DLL_SHA256)):
                self.leases.enter_context(file_lease(path))
                if loader.digest(path)!=pin:raise ValueError('Exact-water observer deployment identity changed.')
            verify_build(self.dll.parent);self.prepared=True
        except BaseException:
            self.close();raise

    def initialize(self,owned):
        if (not self.prepared or owned.executable!=self.executable or owned.native_initialized is not True
                or owned.kernel_isolation!=7):
            raise RuntimeError('Exact-water observer requires the fully isolated owned original child.')
        initialized=loader.invoke_water(owned,self.dll,self.config,pow_cap=self.pow_cap,benchmark=self.benchmark)
        initialized.update(dict(format='s7-water-on12-observer-init-v1',read_only=not self.pow_cap,pow_cap=self.pow_cap,benchmark=self.benchmark,
                                dll_sha256=self.build['files'][DLL]))
        recover._write_new(self.output/'water-probe-initialized.private.json',json.dumps(initialized,indent=2).encode())

    def close(self):
        self.prepared=False;self.leases.close()
