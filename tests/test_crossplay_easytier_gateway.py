"""Real socket forwarding and mocked tunnel ownership; no VPN/game is launched."""
from pathlib import Path
import socket,sys,time,types,unittest,struct
from unittest.mock import Mock,patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/gold'),str(ROOT/'tools/installer')]
import easytier_gateway as gateway
from internet_room import Room
from lobby_directory import LanInterface

ROOM=Room('a'*32,'b'*64,'10.203.77.0/24')

def local_pair():
    # Windows' socketpair fallback can wait without a deadline. Use an explicit
    # IPv4-only pair with bounded connect/accept, including in restricted hosts.
    listener=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    client=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    try:
        listener.settimeout(2);client.settimeout(2)
        listener.bind(('127.0.0.1',0));listener.listen(1)
        client.connect(listener.getsockname());server,_=listener.accept()
        client.settimeout(None);server.settimeout(None)
        return client,server
    except BaseException:
        client.close();raise
    finally:listener.close()

def overlay(host=True):
    own=ROOM.coordinator if host else '10.203.77.2'
    peer='10.203.77.2' if host else ROOM.coordinator
    result=types.SimpleNamespace(host=host,crossplay=True,address=own,device_name='WePlay-test',room=ROOM,child=Mock())
    result.child.poll.return_value=None
    result.snapshot=Mock(return_value=(LanInterface(own,24,result.device_name),{peer:'p2p'}))
    return result,peer

class PartialSocket:
    """Real loopback socket with deliberately short successful send results."""
    def __init__(self,sock):self.sock=sock;self.shortened=False;self.zero=False
    def __getattr__(self,name):return getattr(self.sock,name)
    def send(self,data):
        if self.zero:return 0
        self.shortened=self.shortened or len(data)>64
        return self.sock.send(data[:64])

class ForwardingTests(unittest.TestCase):
    def setUp(self):
        self.left,self.a=local_pair();self.b,self.right=local_pair()
        self.left.setblocking(False);self.right.setblocking(False)
        self.short_right=PartialSocket(self.b)
        self.pipe=gateway.StreamBridge(self.a,self.short_right,limit=1024)

    def tearDown(self):
        self.pipe.close();self.left.close();self.right.close()

    def drain(self,sock):
        try:return sock.recv(65536)
        except BlockingIOError:return None

    def until(self,predicate):
        deadline=time.monotonic()+5
        while not predicate():
            self.pipe.pump(.001)
            if time.monotonic()>deadline:self.fail('Bounded stream deadline exceeded')

    def test_opaque_frames_remain_byte_exact_in_both_directions(self):
        forward=b'WPX1\x00\xff\xfe'+bytes(range(256))*2
        reverse=b'\x80\x00response'+bytes(range(255,-1,-1))
        self.left.sendall(forward);self.right.sendall(reverse)
        got_left,got_right=bytearray(),bytearray()
        def complete():
            for sock,output in ((self.left,got_left),(self.right,got_right)):
                data=self.drain(sock)
                if data:output.extend(data)
            return bytes(got_left)==reverse and bytes(got_right)==forward
        self.until(complete)
        self.assertEqual(sum(self.pipe.bytes_forwarded),len(forward)+len(reverse))

    def test_eof_drains_queued_bytes_and_keeps_reverse_half_open(self):
        request=b'original-frame'+bytes(range(256))*4
        self.left.sendall(request);self.left.shutdown(socket.SHUT_WR)
        received=bytearray();eof=False
        def delivered():
            nonlocal eof
            data=self.drain(self.right)
            if data==b'':eof=True
            elif data:received.extend(data)
            return eof
        self.until(delivered);self.assertEqual(bytes(received),request)
        response=b'last-authenticated-response'
        self.right.sendall(response);self.right.shutdown(socket.SHUT_WR)
        received=bytearray();eof=False
        def replied():
            nonlocal eof
            data=self.drain(self.left)
            if data==b'':eof=True
            elif data:received.extend(data)
            return eof and self.pipe.closed
        self.until(replied);self.assertEqual(bytes(received),response)

    def test_backpressure_is_bounded_and_partial_writes_preserve_order(self):
        self.left.setsockopt(socket.SOL_SOCKET,socket.SO_SNDBUF,4096)
        self.b.setsockopt(socket.SOL_SOCKET,socket.SO_SNDBUF,4096)
        payload=bytes(range(256))*1024
        sent=0;received=bytearray();iteration=0;maximum_pending=0
        def transferred():
            nonlocal sent,iteration,maximum_pending
            iteration+=1
            if sent<len(payload):
                try:sent+=self.left.send(payload[sent:sent+8192])
                except BlockingIOError:pass
            if iteration>100:
                data=self.drain(self.right)
                if data:received.extend(data)
            self.assertTrue(all(len(end.pending)<=1024 for end in self.pipe.ends))
            maximum_pending=max(maximum_pending,max(len(end.pending) for end in self.pipe.ends))
            return len(received)==len(payload)
        self.until(transferred)
        self.assertEqual(maximum_pending,1024);self.assertTrue(self.short_right.shortened)
        self.assertEqual(bytes(received),payload)

    def test_zero_byte_successful_send_closes_without_an_unbounded_retry(self):
        self.short_right.zero=True;self.left.sendall(b'frame')
        with self.assertRaises(gateway.GatewayError):self.until(lambda:False)
        self.assertTrue(self.pipe.closed)

    def test_close_is_idempotent_and_invalid_bounds_are_rejected(self):
        self.pipe.close();self.pipe.close();self.pipe.pump()
        self.assertTrue(self.pipe.closed)
        for value in (0,True,1023,gateway.MAX_BUFFERED+1):
            with self.assertRaises(ValueError):gateway.StreamBridge(self.a,self.b,limit=value)

class TunnelTests(unittest.TestCase):
    def windows_api(self, *, alias_error=0, index_error=0, index=31):
        api=Mock()
        def alias(name,luid):
            self.assertEqual(name,'WePlay-test')
            luid._obj.value=0x1234567887654321
            return alias_error
        def resolve(luid,result):
            self.assertEqual(luid._obj.value,0x1234567887654321)
            result._obj.value=index
            return index_error
        api.ConvertInterfaceAliasToLuid.side_effect=alias
        api.ConvertInterfaceLuidToIndex.side_effect=resolve
        return api

    def test_windows_pinning_resolves_full_luid_and_verifies_host_order_readback(self):
        api,sock=self.windows_api(),Mock()
        sock.getsockopt.return_value=struct.pack('=I',31)
        with patch.object(gateway.sys,'platform','win32'),patch.object(gateway.ctypes,'WinDLL',return_value=api,create=True) as library:
            gateway.pin_interface(sock,LanInterface('10.203.77.2',24,'WePlay-test'))
        library.assert_called_once_with('iphlpapi.dll',use_last_error=True,winmode=0x800)
        sock.setsockopt.assert_called_once_with(socket.IPPROTO_IP,31,struct.pack('!I',31))
        sock.getsockopt.assert_called_once_with(socket.IPPROTO_IP,31,4)
        self.assertEqual(api.ConvertInterfaceAliasToLuid.restype,gateway.ctypes.c_uint32)
        self.assertEqual(api.ConvertInterfaceLuidToIndex.restype,gateway.ctypes.c_uint32)

    def test_windows_pinning_excludes_resolution_failures_and_invalid_indices(self):
        for values in ({'alias_error':1},{'index_error':1},{'index':0},{'index':1<<24}):
            with self.subTest(values=values):
                api,sock=self.windows_api(**values),Mock()
                with patch.object(gateway.sys,'platform','win32'),patch.object(gateway.ctypes,'WinDLL',return_value=api,create=True):
                    with self.assertRaises(gateway.GatewayError):gateway.pin_interface(sock,LanInterface('10.203.77.2',24,'WePlay-test'))
                sock.setsockopt.assert_not_called()

    def test_windows_pinning_rejects_default_wrong_or_truncated_readback(self):
        for result in (struct.pack('=I',0),struct.pack('=I',32),struct.pack('!I',31),b'\x1f'):
            api,sock=self.windows_api(),Mock();sock.getsockopt.return_value=result
            with patch.object(gateway.sys,'platform','win32'),patch.object(gateway.ctypes,'WinDLL',return_value=api,create=True):
                with self.assertRaises(gateway.GatewayError):gateway.pin_interface(sock,LanInterface('10.203.77.2',24,'WePlay-test'))

    def test_requires_owned_live_opt_in_tunnel_and_verified_peer(self):
        o,peer=overlay();self.assertEqual(gateway.tunnel_state(o,peer).address,o.address)
        o.crossplay=False
        with self.assertRaises(gateway.GatewayError):gateway.tunnel_state(o,peer)
        o.crossplay=True;o.child.poll.return_value=0
        with self.assertRaises(gateway.GatewayError):gateway.tunnel_state(o,peer)
        o.child.poll.return_value=None
        for invalid in ('10.203.77.9','192.168.1.2',o.address):
            with self.assertRaises(gateway.GatewayError):gateway.tunnel_state(o,invalid)

    def test_wrong_adapter_or_guest_coordinator_cannot_be_used(self):
        o,peer=overlay(False)
        o.snapshot.return_value=(LanInterface(o.address,24,'Other device'),{peer:'p2p'})
        with self.assertRaises(gateway.GatewayError):gateway.tunnel_state(o,peer)
        o.snapshot.return_value=(LanInterface(o.address,24,o.device_name),{'10.203.77.3':'p2p'})
        with self.assertRaises(gateway.GatewayError):gateway.tunnel_state(o,'10.203.77.3')

    def test_host_binds_only_verified_tun_and_forwards_to_existing_broker_namespace(self):
        o,peer=overlay();listener,accepted,target=Mock(),Mock(),Mock()
        listener.getsockname.return_value=(o.address,gateway.PORT)
        listener.accept.return_value=(accepted,(peer,12345))
        with patch.object(gateway.socket,'socket',side_effect=[listener,target]),patch.object(gateway,'pin_interface') as pin,patch.object(gateway,'StreamBridge') as pair:
            pair.return_value.closed=False
            g=gateway.EasyTierGateway(o,peer=peer,broker_port=49155)
            listener.bind.assert_called_once_with((o.address,9113))
            g.pump();target.bind.assert_called_once_with(('127.0.0.3',0))
            target.connect.assert_called_once_with(('127.0.0.1',49155))
            self.assertEqual(pin.call_count,2);pair.assert_called_once_with(accepted,target)
            g.close();o.child.terminate.assert_not_called()

    def test_guest_keeps_native_listener_loopback_and_pins_vpn_backend(self):
        o,peer=overlay(False);listener,accepted,target=Mock(),Mock(),Mock()
        listener.getsockname.return_value=('127.0.0.1',49154)
        listener.accept.return_value=(accepted,('127.0.0.3',12345))
        with patch.object(gateway.socket,'socket',side_effect=[listener,target]),patch.object(gateway,'pin_interface') as pin,patch.object(gateway,'StreamBridge') as pair:
            pair.return_value.closed=False
            g=gateway.EasyTierGateway(o,peer=peer)
            listener.bind.assert_called_once_with(('127.0.0.1',0))
            g.pump();target.bind.assert_called_once_with((o.address,0))
            target.connect.assert_called_once_with((ROOM.coordinator,9113))
            pin.assert_called_once_with(target,g.interface);g.close()

    def test_unexpected_incoming_source_closes_without_connecting_or_fallback(self):
        o,peer=overlay(False);listener,accepted=Mock(),Mock()
        listener.getsockname.return_value=('127.0.0.1',49154)
        listener.accept.return_value=(accepted,('127.0.0.2',12345))
        with patch.object(gateway.socket,'socket',return_value=listener) as factory:
            g=gateway.EasyTierGateway(o,peer=peer)
            with self.assertRaises(gateway.GatewayError):g.pump()
            self.assertTrue(g.closed);self.assertEqual(factory.call_count,1);accepted.close.assert_called_once()

    def test_tunnel_process_exit_closes_owned_sockets_without_stopping_tunnel_owner(self):
        o,peer=overlay(False);listener=Mock();listener.getsockname.return_value=('127.0.0.1',49154)
        with patch.object(gateway.socket,'socket',return_value=listener):
            g=gateway.EasyTierGateway(o,peer=peer);o.child.poll.return_value=0
            with self.assertRaises(gateway.GatewayError):g.pump()
            self.assertTrue(g.closed);listener.accept.assert_not_called();o.child.terminate.assert_not_called()

    def test_unsupported_interface_pinning_has_no_unpinned_fallback(self):
        with patch.object(gateway.sys,'platform','unsupported'):
            sock=Mock()
            with self.assertRaises(gateway.GatewayError):gateway.pin_interface(sock,LanInterface('10.203.77.2',24,'WePlay-test'))
            sock.setsockopt.assert_not_called()

if __name__=='__main__':unittest.main()
