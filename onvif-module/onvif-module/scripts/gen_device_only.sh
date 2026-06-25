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

# Auto-detect system gSOAP import directory (differs by distro/version/installation)
GSOAP_SYS_IMPORT=""

# 1. Check common standard paths
for candidate in \
    /usr/share/gsoap/import \
    /usr/local/share/gsoap/import \
    /usr/include/gsoap \
    /usr/local/include/gsoap \
    /usr/include \
    /usr/local/include; do
    if [ -f "$candidate/xsi.h" ]; then
        GSOAP_SYS_IMPORT="$candidate"
        break
    fi
done

# 2. Check relative to wsdl2h binary path (if installed from source or custom prefix)
if [ -z "$GSOAP_SYS_IMPORT" ]; then
    WSDL2H_PATH=$(command -v wsdl2h 2>/dev/null)
    if [ -n "$WSDL2H_PATH" ]; then
        BIN_DIR=$(dirname "$WSDL2H_PATH")
        PREFIX_DIR=$(dirname "$BIN_DIR")
        if [ -f "$PREFIX_DIR/share/gsoap/import/xsi.h" ]; then
            GSOAP_SYS_IMPORT="$PREFIX_DIR/share/gsoap/import"
        elif [ -f "$PREFIX_DIR/import/xsi.h" ]; then
            GSOAP_SYS_IMPORT="$PREFIX_DIR/import"
        fi
    fi
fi

# 3. Search in user's home directory (max depth 5 for speed)
if [ -z "$GSOAP_SYS_IMPORT" ]; then
    echo "[INFO] Searching home directory for xsi.h..."
    FOUND_PATH=$(find "$HOME" -maxdepth 5 -name "xsi.h" 2>/dev/null | head -n 1)
    if [ -n "$FOUND_PATH" ]; then
        GSOAP_SYS_IMPORT=$(dirname "$FOUND_PATH")
    fi
fi

# 4. Search in /usr and /usr/local (max depth 5)
if [ -z "$GSOAP_SYS_IMPORT" ]; then
    echo "[INFO] Searching /usr and /usr/local for xsi.h..."
    FOUND_PATH=$(find /usr /usr/local -maxdepth 5 -name "xsi.h" 2>/dev/null | head -n 1)
    if [ -n "$FOUND_PATH" ]; then
        GSOAP_SYS_IMPORT=$(dirname "$FOUND_PATH")
    fi
fi

if [ -z "$GSOAP_SYS_IMPORT" ]; then
    echo "[ERROR] Cannot find gSOAP import dir (xsi.h not found)."
    echo "        Please find where xsi.h is located on your server and set it manually."
    exit 1
fi

echo "[INFO] Found gSOAP import dir: $GSOAP_SYS_IMPORT"

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
