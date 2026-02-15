#!/bin/bash
# Generate self-signed SSL certificates for LinuxCam

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SSL_DIR="$(dirname "$SCRIPT_DIR")/ssl"

mkdir -p "$SSL_DIR"

openssl req -x509 -newkey rsa:4096 -nodes \
    -keyout "$SSL_DIR/key.pem" \
    -out "$SSL_DIR/cert.pem" \
    -days 3650 \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    >/dev/null 2>&1

chmod 600 "$SSL_DIR/key.pem"
chmod 644 "$SSL_DIR/cert.pem"

[ -f "$SSL_DIR/cert.pem" ] && [ -f "$SSL_DIR/key.pem" ] && echo "OK" || echo "KO"
