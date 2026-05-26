#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRIVATE_KEY="${OTA_PRIVATE_KEY:-$ROOT_DIR/.ota-keys/esp32-s3-ota-private.pem}"
PUBLIC_KEY="${OTA_PUBLIC_KEY:-$ROOT_DIR/.ota-keys/esp32-s3-ota-public.pem}"
APP_BIN="${1:-}"
SIGNED_BIN="${2:-}"
SIGNATURE_BYTES=512

if [[ -z "$APP_BIN" ]]; then
  echo "Usage: $0 path/to/application.ino.bin [output.ota.signed.bin]" >&2
  exit 2
fi
if [[ ! -f "$APP_BIN" ]]; then
  echo "Application binary not found: $APP_BIN" >&2
  exit 2
fi
if [[ ! -f "$PRIVATE_KEY" ]]; then
  echo "OTA private key not found: $PRIVATE_KEY" >&2
  echo "Set OTA_PRIVATE_KEY to the securely stored ESP32-S3 OTA signing key." >&2
  exit 2
fi
if [[ ! -f "$PUBLIC_KEY" ]]; then
  echo "OTA public key not found: $PUBLIC_KEY" >&2
  echo "Set OTA_PUBLIC_KEY to the matching ESP32-S3 OTA public key." >&2
  exit 2
fi
if [[ -z "$SIGNED_BIN" ]]; then
  SIGNED_BIN="${APP_BIN%.bin}.ota.signed.bin"
fi

TMP_SIG="$(mktemp "${TMPDIR:-/tmp}/u1-argus-ota-signature.XXXXXX")"
trap 'rm -f "$TMP_SIG"' EXIT

openssl dgst -sha256 -sign "$PRIVATE_KEY" -out "$TMP_SIG" "$APP_BIN"
openssl dgst -sha256 -verify "$PUBLIC_KEY" -signature "$TMP_SIG" "$APP_BIN" >/dev/null
SIG_SIZE="$(wc -c < "$TMP_SIG" | tr -d ' ')"
if (( SIG_SIZE > SIGNATURE_BYTES )); then
  echo "Signature is unexpectedly larger than the OTA trailer: $SIG_SIZE bytes" >&2
  exit 1
fi

cp "$APP_BIN" "$SIGNED_BIN"
cat "$TMP_SIG" >> "$SIGNED_BIN"
dd if=/dev/zero bs=1 count=$((SIGNATURE_BYTES - SIG_SIZE)) >> "$SIGNED_BIN" 2>/dev/null

echo "Signed OTA image: $SIGNED_BIN"
echo "Application bytes: $(wc -c < "$APP_BIN" | tr -d ' ')"
echo "Signature trailer: $SIGNATURE_BYTES bytes (ECDSA signature $SIG_SIZE bytes plus padding)"
echo "Total bytes: $(wc -c < "$SIGNED_BIN" | tr -d ' ')"
