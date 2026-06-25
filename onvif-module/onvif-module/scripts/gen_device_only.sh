#!/bin/bash
# scripts/gen_device_only.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
WSDL_DIR="$ROOT_DIR/wsdl"
GEN_DIR="$ROOT_DIR/generated"
EXT_DIR="$ROOT_DIR/external/gsoap"

mkdir -p "$GEN_DIR" "$WSDL_DIR"

for tool in wsdl2h soapcpp2; do
    command -v $tool &>/dev/null || {
        echo "[ERROR] $tool not found. Run: sudo apt install gsoap"
        exit 1
    }
done

# Auto-detect system gSOAP import directory (differs by distro/version)
GSOAP_SYS_IMPORT=""
for candidate in \
    /usr/share/gsoap/import \
    /usr/local/share/gsoap/import \
    /usr/include/gsoap \
    /usr/local/include/gsoap; do
    if [ -f "$candidate/xsi.h" ]; then
        GSOAP_SYS_IMPORT="$candidate"
        echo "[INFO] Found gSOAP system import dir: $GSOAP_SYS_IMPORT"
        break
    fi
done

if [ -z "$GSOAP_SYS_IMPORT" ]; then
    echo "[ERROR] Cannot find gSOAP import dir (xsi.h not found)."
    echo "        Try: sudo apt install gsoap"
    exit 1
fi

echo "[STEP 1] wsdl2h: devicemgmt.wsdl → C++ header..."
wsdl2h \
    -o "$GEN_DIR/onvif.h" \
    -t "$EXT_DIR/typemap.dat" \
    -c++11 \
    -j \
    -x \
    "$WSDL_DIR/devicemgmt.wsdl"

echo "[STEP 1] Done: $GEN_DIR/onvif.h ($(wc -l < $GEN_DIR/onvif.h) lines)"

echo "[STEP 2] soapcpp2: header → serializers + stubs..."
cd "$GEN_DIR"
soapcpp2 \
    -2 \
    -j \
    -x \
    -I "$GSOAP_SYS_IMPORT" \
    -I "$EXT_DIR/import" \
    -d "$GEN_DIR" \
    "$GEN_DIR/onvif.h"

echo "[STEP 2] Generated files:"
ls -lh "$GEN_DIR/"

echo "[STEP 3] Patching..."
bash "$SCRIPT_DIR/patch_generated.sh"

echo "[DONE] Code generation complete!"
