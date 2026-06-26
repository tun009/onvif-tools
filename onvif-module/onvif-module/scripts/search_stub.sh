#!/bin/bash
# scripts/search_stub.sh
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR"

echo "=== VideoSourceConfiguration ===" > docs/stub_info.txt
grep -A 30 "struct ns1__VideoSourceConfiguration" generated/soapStub.h >> docs/stub_info.txt 2>&1

echo "=== VideoEncoder2Configuration ===" >> docs/stub_info.txt
grep -A 40 "struct ns1__VideoEncoder2Configuration" generated/soapStub.h >> docs/stub_info.txt 2>&1

echo "=== VideoResolution2 ===" >> docs/stub_info.txt
grep -A 20 "struct ns1__VideoResolution2" generated/soapStub.h >> docs/stub_info.txt 2>&1

echo "=== VideoRateControl2 ===" >> docs/stub_info.txt
grep -A 20 "struct ns1__VideoRateControl2" generated/soapStub.h >> docs/stub_info.txt 2>&1

echo "=== IntRectangle ===" >> docs/stub_info.txt
grep -A 20 "struct tt__IntRectangle" generated/soapStub.h >> docs/stub_info.txt 2>&1

echo "=== GetVideoSourceConfigurationsResponse ===" >> docs/stub_info.txt
grep -A 20 "struct _ns1__GetVideoSourceConfigurationsResponse" generated/soapStub.h >> docs/stub_info.txt 2>&1

echo "=== GetVideoEncoderConfigurationsResponse ===" >> docs/stub_info.txt
grep -A 20 "struct _ns1__GetVideoEncoderConfigurationsResponse" generated/soapStub.h >> docs/stub_info.txt 2>&1

echo "Done, results in docs/stub_info.txt"
