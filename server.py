#!/usr/bin/env python3
import http.server
import http.client
import ssl
import os
import socketserver
import socket
import json
import time
import secrets
from pathlib import Path
from urllib.parse import urlsplit, parse_qs
import ipaddress

try:
    from fido2.server import Fido2Server
    from fido2.webauthn import (
        AttestedCredentialData,
        PublicKeyCredentialRpEntity,
        PublicKeyCredentialUserEntity,
        CollectedClientData,
        AttestationObject,
        AuthenticatorData,
    )
    from fido2.utils import websafe_encode, websafe_decode
except ModuleNotFoundError as e:
    if e.name == "fido2":
        print("ERROR: Missing Python dependency 'fido2'.")
        print("\nFix (recommended): use the workspace venv interpreter:")
        print("  ./.venv/bin/python server.py")
        print("\nOr install into your current interpreter:")
        print("  python3 -m pip install fido2")
        raise SystemExit(1)
    raise

BASE_DIR = Path(__file__).resolve().parent

WEBAUTHN_CREDS_PATH = BASE_DIR / "webauthn_credentials.json"


def _now_s() -> float:
    return time.time()


def _load_webauthn_creds() -> dict:
    try:
        with WEBAUTHN_CREDS_PATH.open("r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict):
            return data
    except FileNotFoundError:
        pass
    except Exception as e:
        print(f"WARN: failed to load {WEBAUTHN_CREDS_PATH}: {e}")
    return {}


def _save_webauthn_creds(data: dict) -> None:
    tmp = WEBAUTHN_CREDS_PATH.with_suffix(".json.tmp")
    with tmp.open("w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    tmp.replace(WEBAUTHN_CREDS_PATH)


WEBAUTHN_STORE = _load_webauthn_creds()

# In-memory state for begin/finish flows + short-lived auth token
WEBAUTHN_PENDING: dict[str, dict] = {}
WEBAUTHN_AUTH_TOKENS: dict[str, dict] = {}


def _get_rp_id(host_header: str) -> str:
    host = (host_header or "localhost").strip()
    # host may include port
    if ":" in host:
        host = host.split(":", 1)[0]
    return host or "localhost"


def _server_for_host(host_header: str) -> Fido2Server:
    rp_id = _get_rp_id(host_header)
    rp = PublicKeyCredentialRpEntity(id=rp_id, name="Rollcall")
    return Fido2Server(rp)


def _b64u_str(data: bytes) -> str:
    v = websafe_encode(data)
    return v.decode("ascii") if isinstance(v, (bytes, bytearray)) else str(v)


def _normalize_pk_for_json(pk: dict) -> dict:
    """Ensure challenge/user.id/credential ids are JSON-safe base64url strings.

    Newer python-fido2 already returns base64url strings; older shapes may contain bytes.
    """
    out = dict(pk)
    if isinstance(out.get("challenge"), (bytes, bytearray)):
        out["challenge"] = _b64u_str(out["challenge"])  # type: ignore[arg-type]

    user = out.get("user")
    if isinstance(user, dict) and isinstance(user.get("id"), (bytes, bytearray)):
        user2 = dict(user)
        user2["id"] = _b64u_str(user2["id"])  # type: ignore[arg-type]
        out["user"] = user2

    for key in ("excludeCredentials", "allowCredentials"):
        creds = out.get(key)
        if isinstance(creds, list):
            fixed = []
            for c in creds:
                if isinstance(c, dict) and isinstance(c.get("id"), (bytes, bytearray)):
                    c2 = dict(c)
                    c2["id"] = _b64u_str(c2["id"])  # type: ignore[arg-type]
                    fixed.append(c2)
                else:
                    fixed.append(c)
            out[key] = fixed
    return out


def _creds_for_student(student_id: str) -> list[AttestedCredentialData]:
    raw = WEBAUTHN_STORE.get(student_id)
    if not raw:
        return []
    creds = []
    items = raw if isinstance(raw, list) else [raw]
    for it in items:
        try:
            # Preferred format: store full AttestedCredentialData bytes
            if "attested_cred_data" in it:
                acd = websafe_decode(it["attested_cred_data"])
                creds.append(AttestedCredentialData(acd))
                continue
            # Legacy/unknown formats are ignored
        except Exception:
            continue
    return creds


def _store_credential(student_id: str, cred: AttestedCredentialData) -> None:
    # In fido2 1.2.x, AttestedCredentialData.public_key is a CoseKey (not bytes).
    # The simplest portable storage is the full AttestedCredentialData bytes.
    entry = {"attested_cred_data": _b64u_str(bytes(cred))}
    # For demo: 1 student -> 1 credential. Overwrite if re-register.
    WEBAUTHN_STORE[student_id] = entry
    _save_webauthn_creds(WEBAUTHN_STORE)


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


def _json_response(handler: http.server.BaseHTTPRequestHandler, status: int, body_obj: dict):
    body = json.dumps(body_obj, ensure_ascii=False).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(body)))
    handler.send_header("Connection", "close")
    handler.end_headers()
    handler.wfile.write(body)


def _read_json_body(handler: http.server.BaseHTTPRequestHandler) -> dict | None:
    try:
        content_length = int(handler.headers.get("Content-Length", "0"))
    except ValueError:
        content_length = 0
    if content_length <= 0:
        return {}
    raw = handler.rfile.read(content_length)
    try:
        data = json.loads(raw.decode("utf-8"))
        if isinstance(data, dict):
            return data
        return None
    except Exception:
        return None

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

    def _handle_webauthn_register_begin(self):
        data = _read_json_body(self)
        if data is None:
            return _json_response(self, 400, {"ok": False, "error": "bad_json"})
        student_id = str((data.get("student_id") or "")).strip()
        if not student_id:
            return _json_response(self, 400, {"ok": False, "error": "missing_student_id"})

        server = _server_for_host(self.headers.get("Host", ""))

        # For demo: use student_id as user handle bytes
        user = PublicKeyCredentialUserEntity(
            id=student_id.encode("utf-8"),
            name=student_id,
            display_name=student_id,
        )

        existing = _creds_for_student(student_id)
        options, state = server.register_begin(
            user,
            credentials=existing,
            user_verification="required",
        )

        WEBAUTHN_PENDING[student_id] = {
            "kind": "register",
            "state": state,
            "ts": _now_s(),
            "rp_id": _get_rp_id(self.headers.get("Host", "")),
        }

        # python-fido2 returns CredentialCreationOptions with a top-level 'publicKey'
        pk = options["publicKey"]
        return _json_response(self, 200, {"ok": True, "publicKey": _normalize_pk_for_json(pk)})

    def _handle_webauthn_register_finish(self):
        data = _read_json_body(self)
        if data is None:
            return _json_response(self, 400, {"ok": False, "error": "bad_json"})
        student_id = str((data.get("student_id") or "")).strip()
        if not student_id:
            return _json_response(self, 400, {"ok": False, "error": "missing_student_id"})

        pending = WEBAUTHN_PENDING.get(student_id)
        if not pending or pending.get("kind") != "register":
            return _json_response(self, 400, {"ok": False, "error": "no_pending_register"})
        if _now_s() - float(pending.get("ts", 0)) > 120:
            WEBAUTHN_PENDING.pop(student_id, None)
            return _json_response(self, 400, {"ok": False, "error": "register_timeout"})

        rp_id = pending.get("rp_id")
        if rp_id != _get_rp_id(self.headers.get("Host", "")):
            return _json_response(self, 400, {"ok": False, "error": "rp_id_mismatch"})

        try:
            cred = data.get("credential") or {}
            resp = cred.get("response") or {}

            raw_id = cred.get("rawId") or ""
            if not isinstance(raw_id, str) or not raw_id:
                raise ValueError("missing rawId")

            # Pass JSON-shaped mapping (base64url strings). In fido2 1.2.x-dev,
            # RegistrationResponse.from_dict validates id==rawId if 'id' exists.
            # Omit 'id' and provide 'rawId' only.
            response = {
                "rawId": raw_id,
                "type": cred.get("type", "public-key"),
                "response": {
                    "clientDataJSON": resp["clientDataJSON"],
                    "attestationObject": resp["attestationObject"],
                },
            }

            server = _server_for_host(self.headers.get("Host", ""))
            auth_data = server.register_complete(pending["state"], response)
            credential_data = auth_data.credential_data
            if credential_data is None:
                raise ValueError("missing credential_data")

            _store_credential(student_id, credential_data)
            WEBAUTHN_PENDING.pop(student_id, None)
            return _json_response(self, 200, {"ok": True, "status": "registered"})
        except Exception as e:
            return _json_response(
                self,
                400,
                {"ok": False, "error": "register_failed", "detail_type": type(e).__name__, "detail": str(e)},
            )

    def _handle_webauthn_auth_begin(self):
        data = _read_json_body(self)
        if data is None:
            return _json_response(self, 400, {"ok": False, "error": "bad_json"})
        student_id = str((data.get("student_id") or "")).strip()
        if not student_id:
            return _json_response(self, 400, {"ok": False, "error": "missing_student_id"})

        creds = _creds_for_student(student_id)
        if not creds:
            return _json_response(self, 409, {"ok": False, "error": "not_registered"})

        server = _server_for_host(self.headers.get("Host", ""))
        options, state = server.authenticate_begin(
            creds,
            user_verification="required",
        )

        WEBAUTHN_PENDING[student_id] = {
            "kind": "auth",
            "state": state,
            "ts": _now_s(),
            "rp_id": _get_rp_id(self.headers.get("Host", "")),
        }

        pk = options["publicKey"]
        return _json_response(self, 200, {"ok": True, "publicKey": _normalize_pk_for_json(pk)})

    def _handle_webauthn_auth_finish(self):
        data = _read_json_body(self)
        if data is None:
            return _json_response(self, 400, {"ok": False, "error": "bad_json"})
        student_id = str((data.get("student_id") or "")).strip()
        if not student_id:
            return _json_response(self, 400, {"ok": False, "error": "missing_student_id"})

        pending = WEBAUTHN_PENDING.get(student_id)
        if not pending or pending.get("kind") != "auth":
            return _json_response(self, 400, {"ok": False, "error": "no_pending_auth"})
        if _now_s() - float(pending.get("ts", 0)) > 60:
            WEBAUTHN_PENDING.pop(student_id, None)
            return _json_response(self, 400, {"ok": False, "error": "auth_timeout"})

        rp_id = pending.get("rp_id")
        if rp_id != _get_rp_id(self.headers.get("Host", "")):
            return _json_response(self, 400, {"ok": False, "error": "rp_id_mismatch"})

        creds = _creds_for_student(student_id)
        if not creds:
            return _json_response(self, 409, {"ok": False, "error": "not_registered"})

        try:
            cred = data.get("credential") or {}
            resp = cred.get("response") or {}

            raw_id = cred.get("rawId") or ""
            if not isinstance(raw_id, str) or not raw_id:
                raise ValueError("missing rawId")

            response = {
                "rawId": raw_id,
                "type": cred.get("type", "public-key"),
                "response": {
                    "clientDataJSON": resp["clientDataJSON"],
                    "authenticatorData": resp["authenticatorData"],
                    "signature": resp["signature"],
                },
            }
            if resp.get("userHandle"):
                response["response"]["userHandle"] = resp["userHandle"]

            server = _server_for_host(self.headers.get("Host", ""))
            server.authenticate_complete(pending["state"], creds, response)

            WEBAUTHN_PENDING.pop(student_id, None)

            token = secrets.token_urlsafe(24)
            WEBAUTHN_AUTH_TOKENS[token] = {"student_id": student_id, "ts": _now_s()}
            return _json_response(self, 200, {"ok": True, "auth_token": token})
        except Exception as e:
            return _json_response(
                self,
                401,
                {"ok": False, "error": "auth_failed", "detail_type": type(e).__name__, "detail": str(e)},
            )

    def _peek_auth_token(self, token: str) -> str | None:
        if not token:
            return None
        item = WEBAUTHN_AUTH_TOKENS.get(token)
        if not item:
            return None
        # short-lived; consume only after successful submit
        age = _now_s() - float(item.get("ts", 0))
        if age > 30:
            WEBAUTHN_AUTH_TOKENS.pop(token, None)
            return None
        return str(item.get("student_id") or "")

    def _consume_auth_token(self, token: str) -> None:
        if token:
            WEBAUTHN_AUTH_TOKENS.pop(token, None)

    def _proxy_attendance_submit_with_verified_student(self):
        # Read original JSON
        data = _read_json_body(self)
        if data is None:
            return _json_response(self, 400, {"ok": False, "error": "bad_json"})

        token = str((data.get("auth_token") or "")).strip()
        verified_student = self._peek_auth_token(token)
        if not verified_student:
            return _json_response(self, 401, {"ok": False, "error": "webauthn_required"})

        # Override student_id regardless of what client sent
        data["student_id"] = verified_student
        data.pop("auth_token", None)

        parsed = urlsplit(self.path)
        path_and_query = parsed.path
        if parsed.query:
            path_and_query += "?" + parsed.query

        body = json.dumps(data, ensure_ascii=False).encode("utf-8")

        forward_headers = {}
        for key, value in self.headers.items():
            k = key.lower()
            if k in {"host", "connection", "proxy-connection", "keep-alive", "upgrade", "content-length"}:
                continue
            forward_headers[key] = value
        forward_headers["Content-Type"] = "application/json"
        forward_headers["Content-Length"] = str(len(body))

        conn = http.client.HTTPConnection(self.backend_host, self.backend_port, timeout=10)
        try:
            conn.request("POST", path_and_query, body=body, headers=forward_headers)
            resp = conn.getresponse()
            resp_body = resp.read()

            # Consume auth token only on successful attendance submit.
            if resp.status == 200:
                self._consume_auth_token(token)

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
        if self.path.startswith("/api/webauthn/status"):
            parsed = urlsplit(self.path)
            q = parse_qs(parsed.query or "")
            student_id = (q.get("student_id", [""])[0] or "").strip()
            if not student_id:
                return _json_response(self, 400, {"ok": False, "error": "missing_student_id"})
            registered = len(_creds_for_student(student_id)) > 0
            # Also expose current rp_id so user understands host binding
            return _json_response(
                self,
                200,
                {"ok": True, "student_id": student_id, "registered": registered, "rp_id": _get_rp_id(self.headers.get("Host", ""))},
            )
        if self.path.startswith("/api/"):
            return self._proxy_to_backend()
        return super().do_GET()

    def do_POST(self):
        if not self._ensure_allowed():
            return
        if self.path == "/api/webauthn/register/begin":
            return self._handle_webauthn_register_begin()
        if self.path == "/api/webauthn/register/finish":
            return self._handle_webauthn_register_finish()
        if self.path == "/api/webauthn/auth/begin":
            return self._handle_webauthn_auth_begin()
        if self.path == "/api/webauthn/auth/finish":
            return self._handle_webauthn_auth_finish()
        if self.path == "/api/attendance/submit":
            return self._proxy_attendance_submit_with_verified_student()
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
