#!/bin/bash
# scripts/gen_gsoap.sh
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
    "$CONDA_PREFIX/share/gsoap/import" \
    "$CONDA_PREFIX/include/gsoap" \
    /usr/share/gsoap/import \
    /usr/local/share/gsoap/import \
    /usr/include/gsoap \
    /usr/local/include/gsoap \
    /usr/include \
    /usr/local/include; do
    if [ -n "$candidate" ] && [ -f "$candidate/xsd.h" ]; then
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
        if [ -f "$PREFIX_DIR/share/gsoap/import/xsd.h" ]; then
            GSOAP_SYS_IMPORT="$PREFIX_DIR/share/gsoap/import"
        elif [ -f "$PREFIX_DIR/import/xsd.h" ]; then
            GSOAP_SYS_IMPORT="$PREFIX_DIR/import"
        fi
    fi
fi

# 3. Search in user's home directory (max depth 10 for conda/custom installs)
if [ -z "$GSOAP_SYS_IMPORT" ]; then
    echo "[INFO] Searching home directory for xsd.h (maxdepth 10)..."
    FOUND_PATH=$(find "$HOME" -maxdepth 10 -name "xsd.h" 2>/dev/null | head -n 1)
    if [ -n "$FOUND_PATH" ]; then
        GSOAP_SYS_IMPORT=$(dirname "$FOUND_PATH")
    fi
fi

# 4. Search in /usr and /usr/local (max depth 10)
if [ -z "$GSOAP_SYS_IMPORT" ]; then
    echo "[INFO] Searching /usr and /usr/local for xsd.h (maxdepth 10)..."
    FOUND_PATH=$(find /usr /usr/local -maxdepth 10 -name "xsd.h" 2>/dev/null | head -n 1)
    if [ -n "$FOUND_PATH" ]; then
        GSOAP_SYS_IMPORT=$(dirname "$FOUND_PATH")
    fi
fi

if [ -z "$GSOAP_SYS_IMPORT" ]; then
    echo "[ERROR] Cannot find gSOAP import dir (xsd.h not found)."
    echo "        Please find where xsd.h is located on your server and set it manually."
    exit 1
fi

# Patch absolute URLs in WSDL files to point to local schema files
echo "[INFO] Patching absolute schema URLs in WSDL files to use local paths..."
for f in "$WSDL_DIR"/*.wsdl; do
    if [ -f "$f" ]; then
        sed -i 's|http://docs.oasis-open.org/wsn/b-2.xsd|../../../ver10/schema/b-2.xsd|g' "$f"
        sed -i 's|https://docs.oasis-open.org/wsn/b-2.xsd|../../../ver10/schema/b-2.xsd|g' "$f"
        sed -i 's|http://docs.oasis-open.org/wsn/t-1.xsd|../../../ver10/schema/t-1.xsd|g' "$f"
        sed -i 's|https://docs.oasis-open.org/wsn/t-1.xsd|../../../ver10/schema/t-1.xsd|g' "$f"
        sed -i 's|http://docs.oasis-open.org/wsrf/bf-2.xsd|../../../ver10/schema/bf-2.xsd|g' "$f"
        sed -i 's|https://docs.oasis-open.org/wsrf/bf-2.xsd|../../../ver10/schema/bf-2.xsd|g' "$f"
        sed -i 's|http://www.w3.org/2005/08/addressing/ws-addr.xsd|../../../ver10/schema/ws-addr.xsd|g' "$f"
        sed -i 's|https://www.w3.org/2005/08/addressing/ws-addr.xsd|../../../ver10/schema/ws-addr.xsd|g' "$f"
        sed -i 's|http://www.w3.org/2001/xml.xsd|../../../ver10/schema/xml.xsd|g' "$f"
        sed -i 's|https://www.w3.org/2001/xml.xsd|../../../ver10/schema/xml.xsd|g' "$f"
        sed -i 's|http://www.w3.org/2005/05/xmlmime.xsd|../../../ver10/schema/xmlmime.xsd|g' "$f"
        sed -i 's|https://www.w3.org/2005/05/xmlmime.xsd|../../../ver10/schema/xmlmime.xsd|g' "$f"
        sed -i 's|http://www.w3.org/2004/08/xop/include.xsd|../../../ver10/schema/xop-include.xsd|g' "$f"
        sed -i 's|https://www.w3.org/2004/08/xop/include.xsd|../../../ver10/schema/xop-include.xsd|g' "$f"
        sed -i 's|http://www.w3.org/2003/05/soap-envelope|../../../ver10/schema/soap-envelope.xsd|g' "$f"
        sed -i 's|https://www.w3.org/2003/05/soap-envelope|../../../ver10/schema/soap-envelope.xsd|g' "$f"
        
        # Additional mappings for WS-Notification / Discovery
        sed -i 's|http://docs.oasis-open.org/wsn/bw-2.wsdl|bw-2.wsdl|g' "$f"
        sed -i 's|https://docs.oasis-open.org/wsn/bw-2.wsdl|bw-2.wsdl|g' "$f"
        sed -i 's|http://docs.oasis-open.org/wsrf/rw-2.wsdl|rw-2.wsdl|g' "$f"
        sed -i 's|https://docs.oasis-open.org/wsrf/rw-2.wsdl|rw-2.wsdl|g' "$f"
        sed -i 's|http://docs.oasis-open.org/wsrf/r-2.xsd|../../../ver10/schema/r-2.xsd|g' "$f"
        sed -i 's|https://docs.oasis-open.org/wsrf/r-2.xsd|../../../ver10/schema/r-2.xsd|g' "$f"
        sed -i 's|http://schemas.xmlsoap.org/ws/2005/04/discovery/ws-discovery.xsd|../../../ver10/schema/ws-discovery.xsd|g' "$f"
        sed -i 's|https://schemas.xmlsoap.org/ws/2005/04/discovery/ws-discovery.xsd|../../../ver10/schema/ws-discovery.xsd|g' "$f"
    fi
done

echo "[STEP 1] wsdl2h: WSDL → C++ header..."
wsdl2h \
    -o "$GEN_DIR/onvif.h" \
    -t "$EXT_DIR/typemap.dat" \
    -I "$GSOAP_SYS_IMPORT" \
    -I "$ROOT_DIR/../../ver10/schema" \
    -I "$WSDL_DIR" \
    -c++11 \
    -j \
    -x \
    "$WSDL_DIR/devicemgmt.wsdl" \
    "$WSDL_DIR/media.wsdl" \
    "$WSDL_DIR/ptz.wsdl" \
    "$WSDL_DIR/imaging.wsdl" \
    "$WSDL_DIR/event.wsdl" \
    "$WSDL_DIR/analytics.wsdl" \
    "$WSDL_DIR/remotediscovery.wsdl"

echo "[STEP 1] Done: $GEN_DIR/onvif.h ($(wc -l < $GEN_DIR/onvif.h) lines)"

# Fix missing xsi.h issue
if grep -q '#import "xsi.h"' "$GEN_DIR/onvif.h"; then
    echo "[INFO] Removing #import \"xsi.h\" from onvif.h to prevent compilation error..."
    sed -i '/#import "xsi.h"/d' "$GEN_DIR/onvif.h"
fi

# Fix missing ns*.h issue
echo "[INFO] Removing dummy namespace imports (#import \"ns*.h\") from onvif.h..."
sed -i -E '/#import "ns[0-9]+\.h"/d' "$GEN_DIR/onvif.h"

echo "[STEP 2] soapcpp2: header → serializers + stubs..."
GSOAP_SYS_PARENT=$(dirname "$GSOAP_SYS_IMPORT")
cd "$GEN_DIR"
soapcpp2 \
    -2 \
    -j \
    -x \
    -I "$GSOAP_SYS_IMPORT" \
    -I "$GSOAP_SYS_PARENT" \
    -I "$EXT_DIR/import" \
    -d "$GEN_DIR" \
    "$GEN_DIR/onvif.h"

echo "[STEP 2] Generated files:"
ls -lh "$GEN_DIR/"

echo "[STEP 3] Patching..."
bash "$SCRIPT_DIR/patch_generated.sh"

echo "[DONE] Code generation complete!"
