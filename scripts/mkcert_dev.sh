#!/usr/bin/env bash
set -euo pipefail

# Generates cert.pem/key.pem for this project using mkcert.
# Requirements:
#   - mkcert installed (brew install mkcert)
#   - mkcert root CA installed on this Mac (mkcert -install)
#
# Notes for Android/WebAuthn:
#   - You must also install mkcert's rootCA.pem on the Android device,
#     otherwise Chrome will show a certificate warning and WebAuthn will fail.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v mkcert >/dev/null 2>&1; then
  echo "ERROR: mkcert not found. Install with: brew install mkcert" >&2
  exit 1
fi

# Best-effort LAN IP (matches server.py printing behavior)
LAN_IP="$(python3 - <<'PY'
import socket
try:
    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    s.connect(('8.8.8.8',80))
    print(s.getsockname()[0])
    s.close()
except Exception:
    print('')
PY
)"

HOSTS=("localhost" "127.0.0.1")
if [[ -n "$LAN_IP" ]]; then
  HOSTS+=("$LAN_IP")
  # Android Chrome often rejects raw IP as WebAuthn RP ID. Use a real domain that resolves to the LAN IP.
  # sslip.io: 192.168.1.12.sslip.io -> 192.168.1.12
  HOSTS+=("${LAN_IP}.sslip.io")
  # nip.io: 192-168-1-12.nip.io -> 192.168.1.12
  HOSTS+=("${LAN_IP//./-}.nip.io")
fi

echo "Using hosts: ${HOSTS[*]}"

# Create cert/key in project root to match server.py
mkcert -key-file key.pem -cert-file cert.pem "${HOSTS[@]}"

CAROOT="$(mkcert -CAROOT)"
echo
echo "Generated: $ROOT_DIR/cert.pem and $ROOT_DIR/key.pem"
echo "mkcert CA root: $CAROOT"
echo
echo "Android step: copy $CAROOT/rootCA.pem to your phone and install it as a CA certificate."
echo "Then open the site using a host that matches the cert (e.g., https://$LAN_IP:8000 if included)."
