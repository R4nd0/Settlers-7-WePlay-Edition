"""Bounded cold-start readiness; no game, service or network is started."""
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(ROOT/'tools/offline-ui')]
import launch_local as launch


class ServiceReadinessTests(unittest.TestCase):
    def test_cold_service_ready_after_thirty_seconds_is_validated_before_use(self):
        now=[0.0];process=Mock();process.poll.return_value=None;log=Mock()
        ready=json.dumps(dict(event='configuration-server-ready',http_port=1234,udp_port=1235))
        log.read_text.side_effect=lambda **kw:ready if now[0]>=30 else ''
        response=Mock(status=200);response.read.return_value=b'{"fixture":true}'
        connection=Mock();connection.getresponse.return_value=response
        with patch.object(launch.time,'monotonic',side_effect=lambda:now[0]), \
             patch.object(launch.time,'sleep',side_effect=lambda seconds:now.__setitem__(0,now[0]+seconds)), \
             patch.object(launch.http.client,'HTTPConnection',return_value=connection) as connect:
            self.assertEqual(launch.service_ready(process,log),({'fixture':True},1235))
        self.assertGreaterEqual(now[0],30);self.assertLess(now[0],60)
        connect.assert_called_once_with('127.0.0.1',1234,timeout=3)
        connection.request.assert_called_once_with('GET','/public/rendezvous/v1/configuration')
        connection.close.assert_called_once()

    def test_never_ready_service_still_times_out_without_connecting(self):
        now=[0.0];process=Mock();process.poll.return_value=None;log=Mock()
        log.read_text.return_value=''
        with patch.object(launch.time,'monotonic',side_effect=lambda:now[0]), \
             patch.object(launch.time,'sleep',side_effect=lambda seconds:now.__setitem__(0,now[0]+seconds)), \
             patch.object(launch.http.client,'HTTPConnection') as connect:
            with self.assertRaises(TimeoutError):launch.service_ready(process,log)
        self.assertGreaterEqual(now[0],60);self.assertLess(now[0],60.2)
        connect.assert_not_called()

    def test_exited_child_fails_immediately(self):
        process=Mock();process.poll.return_value=1
        with patch.object(launch.time,'sleep') as sleep:
            with self.assertRaises(RuntimeError):launch.service_ready(process,Mock())
        sleep.assert_not_called()

    def test_invalid_ready_ports_are_not_admitted(self):
        process=Mock();process.poll.return_value=None;log=Mock()
        log.read_text.return_value=json.dumps(dict(event='configuration-server-ready',http_port=0,udp_port=1235))
        with patch.object(launch.http.client,'HTTPConnection') as connect:
            with self.assertRaises(ValueError):launch.service_ready(process,log)
        connect.assert_not_called()


if __name__=='__main__':unittest.main()
