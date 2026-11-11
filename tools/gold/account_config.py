"""Bounded own loopback configuration origin for Gold's native OnlineConfigClient."""
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from urllib.parse import parse_qs, urlsplit

ROUTE = "/OnlineConfigService.svc/GetOnlineConfig?onlineConfigID=WePlayGoldLoopback&target=client"


class ConfigService(HTTPServer):
    request_queue_size = 8

    def __init__(self, auth_port: int, *, loopback_slot: int = 1):
        if type(loopback_slot) is not int or not 1 <= loopback_slot <= 9:
            raise ValueError("Expected bounded loopback slot")
        self.allowed_client_address = f"127.0.0.{loopback_slot}"
        if not 1024 <= auth_port <= 65535:
            raise ValueError("Expected own loopback auth port")
        self.responses = 0
        self.rejections = {"route": 0, "host": 0, "body": 0}
        self.host_rejections = {"missing": 0, "multiple": 0, "loopback": 0, "localhost": 0, "other": 0}
        self.body = json.dumps([{"Name": "SandboxUrl", "Values": [
            f"prudp:/address=127.0.0.1;port={auth_port};stream=3;sid=1"]}],
            separators=(",", ":")).encode("ascii")
        super().__init__(("127.0.0.1", 0), ConfigHandler)
        self.timeout = .1

    def get_request(self):
        connection, peer = super().get_request()
        connection.settimeout(1.)
        return connection, peer

    def handle_error(self, request, client_address):
        # No request text, headers or identities in diagnostics.
        pass


class ConfigHandler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        self.close_connection = True
        expected_host = f"127.0.0.1:{self.server.server_port}"
        target = urlsplit(self.path)
        try:
            query = parse_qs(target.query, strict_parsing=True, max_num_fields=2)
        except ValueError:
            query = {}
        checks = {
            "route": (not target.scheme and not target.netloc and not target.fragment
                      and target.path == ROUTE.split("?")[0]
                      and query == {"onlineConfigID": ["WePlayGoldLoopback"], "target": ["client"]}),
            "host": (self.client_address[0] == self.server.allowed_client_address
                     and self.headers.get_all("Host", []) in ([expected_host], ["127.0.0.1"])),
            "body": (self.headers.get_all("Content-Length", ["0"]) == ["0"]
                     and self.headers.get("Transfer-Encoding") is None)
        }
        if not all(checks.values()):
            for label, valid in checks.items():
                if not valid:
                    self.server.rejections[label] += 1
            if not checks["host"]:
                hosts = self.headers.get_all("Host", [])
                label = ("missing" if not hosts else "multiple" if len(hosts) != 1 else
                         "loopback" if hosts[0].strip().split(":")[0] == "127.0.0.1" else
                         "localhost" if hosts[0].strip().split(":")[0] == "localhost" else "other")
                self.server.host_rejections[label] += 1
            self.send_error(400)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(self.server.body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(self.server.body)
        self.server.responses += 1
