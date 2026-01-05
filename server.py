#!/usr/bin/env python3
import http.server
import http.client
import ssl
import os
import socketserver
import socket
from pathlib import Path
from urllib.parse import urlsplit
import ipaddress

BASE_DIR = Path(__file__).resolve().parent


def get_lan_ip() -> str:
    """Best-effort LAN IP for printing a phone-friendly URL."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect(("8.8.8.8", 80))
        ip = sock.getsockname()[0]
        sock.close()
        return ip
    except Exception:
        return "localhost"


os.chdir(BASE_DIR)


def parse_cidr_list(raw: str) -> list[ipaddress._BaseNetwork]:
    networks: list[ipaddress._BaseNetwork] = []
    for part in raw.split(','):
        part = part.strip()
        if not part:
            continue
        networks.append(ipaddress.ip_network(part, strict=False))
    return networks


DEFAULT_ALLOWED_CIDRS = [
    ipaddress.ip_network("127.0.0.0/8"),
    ipaddress.ip_network("10.0.0.0/8"),
    ipaddress.ip_network("172.16.0.0/12"),
    ipaddress.ip_network("192.168.0.0/16"),
]


RAW_ALLOWED = os.environ.get("CAMPUS_CIDRS", "").strip()
ALLOWED_NETWORKS = parse_cidr_list(RAW_ALLOWED) if RAW_ALLOWED else DEFAULT_ALLOWED_CIDRS


def is_allowed_client_ip(ip_str: str) -> bool:
    try:
        ip = ipaddress.ip_address(ip_str)
    except ValueError:
        return False
    return any(ip in net for net in ALLOWED_NETWORKS)

class HTTPSHandler(http.server.SimpleHTTPRequestHandler):
    backend_host = "127.0.0.1"
    backend_port = 9000

    def _reject_forbidden(self):
        self.send_response(403)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write(
            "Forbidden: chỉ cho phép truy cập từ mạng được whitelist (CAMPUS_CIDRS).\n".encode("utf-8")
        )

    def _ensure_allowed(self) -> bool:
        client_ip = self.client_address[0]
        if is_allowed_client_ip(client_ip):
            print(f"ALLOW {client_ip} {self.command} {self.path}")
            return True
        print(f"DENY  {client_ip} {self.command} {self.path}")
        self._reject_forbidden()
        return False

    def _proxy_to_backend(self):
        parsed = urlsplit(self.path)
        path_and_query = parsed.path
        if parsed.query:
            path_and_query += "?" + parsed.query

        content_length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(content_length) if content_length else None

        forward_headers = {}
        for key, value in self.headers.items():
            k = key.lower()
            if k in {"host", "connection", "proxy-connection", "keep-alive", "upgrade"}:
                continue
            forward_headers[key] = value

        conn = http.client.HTTPConnection(self.backend_host, self.backend_port, timeout=10)
        try:
            conn.request(self.command, path_and_query, body=body, headers=forward_headers)
            resp = conn.getresponse()
            resp_body = resp.read()

            self.send_response(resp.status)
            for key, value in resp.getheaders():
                k = key.lower()
                if k in {"transfer-encoding", "connection"}:
                    continue
                self.send_header(key, value)
            self.end_headers()
            if resp_body:
                self.wfile.write(resp_body)
        finally:
            conn.close()

    def do_GET(self):
        if not self._ensure_allowed():
            return
        if self.path.startswith("/api/"):
            return self._proxy_to_backend()
        return super().do_GET()

    def do_POST(self):
        if not self._ensure_allowed():
            return
        if self.path.startswith("/api/"):
            return self._proxy_to_backend()
        return super().do_POST()

    def do_OPTIONS(self):
        if self.path.startswith("/api/"):
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.end_headers()
            return
        return super().do_OPTIONS()

httpd = socketserver.TCPServer(('', 8000), HTTPSHandler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
cert_path = BASE_DIR / 'cert.pem'
key_path = BASE_DIR / 'key.pem'
context.load_cert_chain(cert_path, key_path)
httpd.socket = context.wrap_socket(httpd.socket, server_side=True)

lan_ip = get_lan_ip()

print("🔒 HTTPS Server chạy tại:")
print("   Mac:    https://localhost:8000/test.html")
print("   Mac:    https://localhost:8000/lecturer.html")
print("   Mac:    https://localhost:8000/student.html")
print(f"   iPhone:  https://{lan_ip}:8000/test.html")
print(f"   iPhone:  https://{lan_ip}:8000/lecturer.html")
print(f"   iPhone:  https://{lan_ip}:8000/student.html")
print("⚠️  Bỏ qua cảnh báo SSL certificate")
print("🔐 Campus filter (CAMPUS_CIDRS):")
print("   " + (RAW_ALLOWED if RAW_ALLOWED else "default: 127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16"))
# thay ip NAT trường vào RAW_ALLOWED
httpd.serve_forever()
