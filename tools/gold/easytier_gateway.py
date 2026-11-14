"""Opt-in transport for the mixed-edition lab channel over an owned EasyTier.

Game sockets remain loopback-only. WPX1 bytes and their end-to-end authenticators
are forwarded unchanged; this module cannot admit a native player or start a map.
No listener is opened on import. Real tunnel/game qualification is separate.
"""
import ctypes
import ipaddress
import selectors
import socket
import struct
import sys
import time

PORT = 9113
MAX_BUFFERED = 8 * 1024 * 1024


class GatewayError(RuntimeError):
    pass


def pin_interface(sock, interface):
    """Pin outgoing packets to the verified TUN; never fall back to another NIC."""
    if sys.platform == 'win32':
        api = ctypes.WinDLL('iphlpapi.dll', use_last_error=True, winmode=0x800)
        luid, index = ctypes.c_uint64(), ctypes.c_uint32()
        alias = api.ConvertInterfaceAliasToLuid
        alias.argtypes = (ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint64))
        alias.restype = ctypes.c_uint32
        resolve = api.ConvertInterfaceLuidToIndex
        resolve.argtypes = (ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_uint32))
        resolve.restype = ctypes.c_uint32
        if (alias(interface.name, ctypes.byref(luid)) or resolve(ctypes.byref(luid), ctypes.byref(index))
                or not 0 < index.value < 1 << 24):
            raise GatewayError('tunnel-interface-unavailable')
        sock.setsockopt(socket.IPPROTO_IP, 31, struct.pack('!I', index.value))  # IP_UNICAST_IF
        # Winsock takes network order when setting this option but returns the
        # selected index in host order. A default or different NIC is excluded.
        actual = sock.getsockopt(socket.IPPROTO_IP, 31, 4)
        if len(actual) != 4 or struct.unpack('=I', actual)[0] != index.value:
            raise GatewayError('tunnel-interface-pinning-not-consumed')
    elif sys.platform.startswith('linux') and hasattr(socket, 'SO_BINDTODEVICE'):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, interface.name.encode('utf-8') + b'\0')
    else:
        raise GatewayError('tunnel-interface-pinning-unavailable')


class _End:
    def __init__(self, sock):
        self.sock, self.pending = sock, bytearray()
        self.eof = self.write_closed = self.registered = False
        self.other = None


class StreamBridge:
    """One bounded duplex stream, including ordered EOF and partial writes."""
    def __init__(self, left, right, *, limit=MAX_BUFFERED):
        if type(limit) is not int or not 1024 <= limit <= MAX_BUFFERED:
            raise ValueError('Invalid stream buffer limit')
        self.limit, self.closed = limit, False
        self.selector = selectors.DefaultSelector()
        self.ends = (_End(left), _End(right))
        self.bytes_forwarded = [0, 0]
        self.ends[0].other, self.ends[1].other = self.ends[1], self.ends[0]
        try:
            for end in self.ends:
                end.sock.setblocking(False)
                self._interest(end)
        except BaseException:
            self.close()
            raise

    def _interest(self, end):
        events = (selectors.EVENT_READ if not end.eof and len(end.other.pending) < self.limit else 0)
        if end.pending:
            events |= selectors.EVENT_WRITE
        if events:
            if end.registered:
                self.selector.modify(end.sock, events, end)
            else:
                self.selector.register(end.sock, events, end)
                end.registered = True
        elif end.registered:
            self.selector.unregister(end.sock)
            end.registered = False

    def pump(self, timeout=0):
        if self.closed:
            return
        try:
            for key, events in self.selector.select(timeout):
                end = key.data
                if events & selectors.EVENT_READ:
                    room = self.limit - len(end.other.pending)
                    if room:
                        try:
                            data = end.sock.recv(min(65536, room))
                        except BlockingIOError:
                            data = None
                        if data == b'':
                            end.eof = True
                        elif data:
                            end.other.pending.extend(data)
                if events & selectors.EVENT_WRITE and end.pending:
                    try:
                        written = end.sock.send(end.pending)
                    except BlockingIOError:
                        written = None
                    if written == 0:
                        self.close()
                        raise GatewayError('tunnel-stream-write-closed')
                    if written:
                        del end.pending[:written]
                        self.bytes_forwarded[self.ends.index(end)] += written
            for end in self.ends:
                if end.other.eof and not end.pending and not end.write_closed:
                    end.sock.shutdown(socket.SHUT_WR)
                    end.write_closed = True
                self._interest(end)
            if all(end.eof and not end.pending for end in self.ends):
                self.close()
        except OSError:
            self.close()
            raise GatewayError('tunnel-stream-closed') from None

    def close(self):
        if not self.closed:
            self.closed = True
            self.selector.close()
            for end in self.ends:
                end.sock.close()


def tunnel_state(overlay, peer):
    if getattr(overlay, 'crossplay', False) is not True or overlay.child is None or overlay.child.poll() is not None:
        raise GatewayError('owned-crossplay-tunnel-required')
    interface, peers = overlay.snapshot()
    address = ipaddress.IPv4Address(peer)
    if (interface.address != overlay.address or interface.name != overlay.device_name
            or interface.prefix_length != 24 or address not in overlay.room.network
            or peer == overlay.address or peer not in peers):
        raise GatewayError('tunnel-peer-or-interface-mismatch')
    if bool(overlay.host) != (overlay.address == overlay.room.coordinator):
        raise GatewayError('tunnel-role-mismatch')
    if not overlay.host and peer != overlay.room.coordinator:
        raise GatewayError('tunnel-coordinator-required')
    return interface


class EasyTierGateway:
    """One host relay or one guest loopback entrance for a fresh pair session.

    The caller owns the EasyTier child and the native broker separately. Host
    mode needs that broker's ephemeral port and the already observed VPN guest.
    Guest mode returns its loopback `port` for the unchanged native channel.
    """
    def __init__(self, overlay, *, peer, broker_port=None, seconds=1800):
        if type(seconds) is not int or not 15 <= seconds <= 1800:
            raise ValueError('Invalid gateway lifetime')
        if overlay.host and (type(broker_port) is not int or not 1024 <= broker_port <= 65535):
            raise ValueError('Expected the owned loopback broker port')
        if not overlay.host and broker_port is not None:
            raise ValueError('Guest cannot choose a broker endpoint')
        self.overlay, self.peer, self.broker_port = overlay, peer, broker_port
        self.interface = tunnel_state(overlay, peer)
        self.deadline, self.checked_at = time.monotonic() + seconds, time.monotonic()
        self.listener, self.bridge, self.closed = None, None, False
        try:
            self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            if overlay.host:
                pin_interface(self.listener, self.interface)
                self.listener.bind((overlay.address, PORT))
            else:
                self.listener.bind(('127.0.0.1', 0))
            self.listener.listen(1)
            self.listener.setblocking(False)
            self.port = self.listener.getsockname()[1]
        except BaseException:
            self.close()
            raise

    def pump(self):
        if self.closed:
            return
        try:
            now = time.monotonic()
            if now >= self.deadline or self.overlay.child.poll() is not None:
                raise GatewayError('tunnel-ended')
            if now - self.checked_at >= 2:
                current = tunnel_state(self.overlay, self.peer)
                if current != self.interface:
                    raise GatewayError('tunnel-interface-changed')
                self.checked_at = now
            if self.bridge is None:
                try:
                    accepted, address = self.listener.accept()
                except BlockingIOError:
                    return
                expected = self.peer if self.overlay.host else '127.0.0.3'
                if address[0] != expected:
                    accepted.close()
                    raise GatewayError('tunnel-source-mismatch')
                target = None
                try:
                    accepted.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
                    target = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    target.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
                    target.settimeout(3)
                    if self.overlay.host:
                        pin_interface(accepted, self.interface)
                        target.bind(('127.0.0.3', 0))
                        target.connect(('127.0.0.1', self.broker_port))
                    else:
                        pin_interface(target, self.interface)
                        target.bind((self.overlay.address, 0))
                        target.connect((self.overlay.room.coordinator, PORT))
                    self.bridge = StreamBridge(accepted, target)
                except BaseException:
                    accepted.close()
                    if target is not None:
                        target.close()
                    raise
                self.listener.close()
                self.listener = None
            self.bridge.pump()
            if self.bridge.closed:
                self.close()
        except BaseException:
            self.close()
            raise

    def close(self):
        if not self.closed:
            self.closed = True
            if self.listener is not None:
                self.listener.close()
                self.listener = None
            if self.bridge is not None:
                self.bridge.close()
