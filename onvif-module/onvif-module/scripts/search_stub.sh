#!/bin/bash
# scripts/search_stub.sh
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR"

# docs/ is at workspace root, which is 2 levels up from onvif-module/onvif-module
OUT_FILE="../../docs/stub_info.txt"

# Ensure the output directory exists
mkdir -p "$(dirname "$OUT_FILE")"

echo "=== VideoSourceConfiguration ===" > "$OUT_FILE"
grep -A 30 "struct ns1__VideoSourceConfiguration" generated/soapStub.h >> "$OUT_FILE" 2>&1

echo "=== VideoEncoder2Configuration ===" >> "$OUT_FILE"
grep -A 40 "struct ns1__VideoEncoder2Configuration" generated/soapStub.h >> "$OUT_FILE" 2>&1

echo "=== VideoResolution2 ===" >> "$OUT_FILE"
grep -A 20 "struct ns1__VideoResolution2" generated/soapStub.h >> "$OUT_FILE" 2>&1

echo "=== VideoRateControl2 ===" >> "$OUT_FILE"
grep -A 20 "struct ns1__VideoRateControl2" generated/soapStub.h >> "$OUT_FILE" 2>&1

echo "=== IntRectangle ===" >> "$OUT_FILE"
grep -A 20 "struct tt__IntRectangle" generated/soapStub.h >> "$OUT_FILE" 2>&1

echo "=== GetVideoSourceConfigurationsResponse ===" >> "$OUT_FILE"
grep -A 20 "struct _ns1__GetVideoSourceConfigurationsResponse" generated/soapStub.h >> "$OUT_FILE" 2>&1

echo "=== GetVideoEncoderConfigurationsResponse ===" >> "$OUT_FILE"
grep -A 20 "struct _ns1__GetVideoEncoderConfigurationsResponse" generated/soapStub.h >> "$OUT_FILE" 2>&1

echo "Done, results in docs/stub_info.txt"
