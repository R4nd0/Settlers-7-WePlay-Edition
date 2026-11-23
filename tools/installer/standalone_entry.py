"""Small x86 entry bridge shared by the three fingerprint-bound starters."""
import struct

FAILURE_EXIT_BASE = 0xe0537080
NETWORK_PROBE_EXIT_PREFIX = 0xe1000000
NETWORK_PROBES = (
    'winsock', 'ipv4_loopback_tcp', 'ipv6_loopback_tcp',
    'ipv4_nonloopback_tcp', 'ipv4_nonloopback_udp', 'ipv6_nonloopback_tcp', 'ipv6_nonloopback_udp',
)
NETWORK_PROBE_STEPS = (
    None, 'startup', 'listener_socket', 'bind', 'listen', 'getsockname', 'client_socket',
    'connect', 'select', 'unexpected_delivery', 'missing_delivery',
)
STAGES = {
    1: 'Adapter laden',
    2: 'Initialisierungsexport auflösen',
    3: 'Nativer Initialisierungseinstieg',
    4: 'Auf Startfreigabe warten',
    10: 'Spielabbild prüfen',
    11: 'Windows-Netzisolation prüfen',
    12: 'Isoliertes Profil vorbereiten',
    13: 'Native Startparameter prüfen',
    14: 'Netzschutz vorbereiten',
    15: 'Parallele Testinstanz isolieren',
    16: 'Nativer Gold-Startadapter',
    17: 'Native Bedienoberfläche anbinden',
    18: 'Nativer Lobbyadapter',
}


def network_probe_failure(exit_code):
    # Keep this encoding in sync with native/standalone_probe_diagnostics.h.
    if exit_code is None or (exit_code & 0xff000000) != NETWORK_PROBE_EXIT_PREFIX:
        return None
    test = (exit_code >> 20) & 0xf
    step = (exit_code >> 16) & 0xf
    if test >= len(NETWORK_PROBES) or not 0 < step < len(NETWORK_PROBE_STEPS) or (test == 0) != (step == 1):
        return None
    return NETWORK_PROBES[test], NETWORK_PROBE_STEPS[step], (exit_code & 0xffff) or None


class NativeStartupError(RuntimeError):
    def __init__(self, last_stage, exit_code):
        self.network_probe_failure = network_probe_failure(exit_code)
        reported_stage = exit_code - FAILURE_EXIT_BASE if exit_code is not None else None
        if self.network_probe_failure is not None:
            reported_stage = 11
        stage = reported_stage if reported_stage in STAGES else last_stage
        self.native_start_stage = stage if stage in STAGES else None
        self.native_exit_code = exit_code
        label = STAGES.get(stage, 'Windows-Programmstart')
        if exit_code is None:
            message = f'Zeitlimit beim nativen Starter. Letzte Stufe „{label}“.'
        elif reported_stage in STAGES:
            message = f'Der native Starter ist in der Stufe „{label}“ fehlgeschlagen.'
        else:
            message = f'Der Spielprozess wurde mit Exitcode 0x{exit_code:08X} beendet. Letzte Startstufe „{label}“.'
        super().__init__(message)


def bridge(remote, image, status, dll, imports, *, initializer='WePlayInitializeStandalone'):
    data = bytearray(4096)
    data[480:490] = b'WPS7ENTRY1'
    at = 512
    constants = {}
    for name, value in [('dll', str(dll).encode('utf-16le') + b'\0\0'),
                        ('init', initializer.encode('ascii') + b'\0')]:
        if at + len(value) > len(data):
            raise ValueError('Der Adapterpfad ist zu lang.')
        constants[name] = remote + at
        data[at:at + len(value)] = value
        at += len(value)
    code, failures, labels = bytearray(), [], {}

    def emit(*values): code.extend(values)
    def word(value): code.extend(struct.pack('<I', value & 0xffffffff))
    def push(value): emit(0x68); word(value)
    def iat(name): emit(0xff, 0x15); word(image + imports[name])
    def fail_if(condition):
        emit(0x0f, condition); failures.append(len(code)); word(0)
    def set_stage(stage):
        emit(0xc7, 5); word(status + 4); word(stage)

    emit(0x9c, 0x60)  # Preserve entry flags and every original register.
    set_stage(1)
    push(constants['dll']); iat('LoadLibraryW'); emit(0x85, 0xc0); fail_if(0x84)
    set_stage(2)
    push(constants['init']); emit(0x50); iat('GetProcAddress'); emit(0x85, 0xc0); fail_if(0x84)
    set_stage(3)
    push(status + 32); emit(0xff, 0xd0, 0x85, 0xc0); fail_if(0x84)
    emit(0xa3); word(status + 8)  # Initializer supplies the verified native OEP.
    set_stage(4)
    emit(0xc7, 5); word(status); word(1)
    iat('GetTickCount'); emit(0x89, 0xc6)
    labels['wait'] = len(code)
    emit(0x83, 0x3d); word(status + 12); emit(1)
    emit(0x0f, 0x84); released = len(code); word(0)
    push(10); iat('Sleep'); iat('GetTickCount')
    emit(0x29, 0xf0, 0x3d); word(15000); fail_if(0x83)
    emit(0xe9); word(labels['wait'] - len(code) - 4)
    labels['released'] = len(code)
    emit(0x61, 0x9d, 0xff, 0x25); word(status + 8)
    failure = len(code)
    emit(0xc7, 5); word(status); word(2)
    emit(0xa1); word(status + 24)  # Optional native socket failure exit code.
    emit(0x85, 0xc0, 0x0f, 0x85); detailed = len(code); word(0)
    emit(0xa1); word(status + 4)  # Preserve the stage for older adapters and other failures.
    emit(0x05); word(FAILURE_EXIT_BASE)
    struct.pack_into('<i', code, detailed, len(code) - detailed - 4)
    emit(0x50, 0x6a, 0xff)
    iat('TerminateProcess'); emit(0xcc)
    for position in failures:
        struct.pack_into('<i', code, position, failure - position - 4)
    struct.pack_into('<i', code, released, labels['released'] - released - 4)
    if len(code) > 480:
        raise ValueError('Der Startercode ist zu groß.')
    data[:len(code)] = code
    return bytes(data)
