#!/bin/bash
# scripts/gen_gsoap.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
WSDL_DIR="$ROOT_DIR/wsdl"
GEN_DIR="$ROOT_DIR/generated"
EXT_DIR="$ROOT_DIR/external/gsoap"

mkdir -p "$GEN_DIR" "$WSDL_DIR"

# Automatically ensure schemas and WSDLs are downloaded and patched
echo "[INFO] Running download_schemas.sh to ensure all schemas are present..."
bash "$SCRIPT_DIR/download_schemas.sh"
echo "[INFO] Running download_wsdls.sh to ensure all WSDL files are present..."
bash "$SCRIPT_DIR/download_wsdls.sh"

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

# Ensure gSOAP runtime files stdsoap2.cpp and stdsoap2.h exist in external/gsoap/
if [ ! -f "$EXT_DIR/stdsoap2.cpp" ] || [ ! -f "$EXT_DIR/stdsoap2.h" ]; then
    echo "[INFO] Copying stdsoap2.cpp and stdsoap2.h to external/gsoap/ (best effort)..."
    
    # Try dynamic find excluding external/gsoap in case it's custom installed
    FOUND_CPP=$(find /usr /usr/local /opt /home -name "stdsoap2.cpp" 2>/dev/null | grep -v "external/gsoap" | head -n 1)
    if [ -n "$FOUND_CPP" ] && [ -f "$FOUND_CPP" ]; then
        GSOAP_SRC=$(dirname "$FOUND_CPP")
        if [ -f "$GSOAP_SRC/stdsoap2.h" ]; then
            cp "$GSOAP_SRC/stdsoap2.h"   "$EXT_DIR/" 2>/dev/null || true
            cp "$GSOAP_SRC/stdsoap2.cpp" "$EXT_DIR/" 2>/dev/null || true
            echo "  Copied stdsoap2 files from $GSOAP_SRC"
        fi
    else
        # Try finding via dpkg
        GSOAP_SRC_FILE=$(dpkg -L libgsoap-dev 2>/dev/null | grep stdsoap2.h | head -1 || true)
        if [ -n "$GSOAP_SRC_FILE" ]; then
            GSOAP_SRC=$(dirname "$GSOAP_SRC_FILE")
            cp "$GSOAP_SRC/stdsoap2.h"   "$EXT_DIR/" 2>/dev/null || true
            cp "$GSOAP_SRC/stdsoap2.cpp" "$EXT_DIR/" 2>/dev/null || true
            echo "  Copied from dpkg package path: $GSOAP_SRC"
        fi
    fi
fi

# Ensure plugins exist in external/gsoap/plugin/
# NOTE: wsseapi.cpp is intentionally NOT copied — it requires XML-DSIG/XML-Enc
# serializer types (ds__SignatureType, xenc__EncryptionMethodType, wsse__FailedCheck)
# that conflict with ONVIF-generated types from soapcpp2.
# WS-Security UsernameToken/PasswordDigest is handled in WsSecurityHandler.cpp
# directly via OpenSSL without wsseapi.
mkdir -p "$EXT_DIR/plugin"
if [ ! -f "$EXT_DIR/plugin/wsddapi.cpp" ]; then
    echo "[INFO] Copying wsddapi (WS-Discovery) plugin only..."
    FOUND_WSDD=$(find /usr /usr/local /opt /home -name "wsddapi.cpp" 2>/dev/null | grep -v "external/gsoap" | head -n 1)
    if [ -n "$FOUND_WSDD" ]; then
        PLUGIN_SRC=$(dirname "$FOUND_WSDD")
        cp "$PLUGIN_SRC/wsddapi.cpp" "$EXT_DIR/plugin/" 2>/dev/null || true
        cp "$PLUGIN_SRC/wsddapi.h"   "$EXT_DIR/plugin/" 2>/dev/null || true
        echo "  Copied wsddapi from $PLUGIN_SRC"
    elif [ -f "/usr/share/gsoap/plugin/wsddapi.cpp" ]; then
        cp /usr/share/gsoap/plugin/wsddapi.cpp "$EXT_DIR/plugin/" 2>/dev/null || true
        cp /usr/share/gsoap/plugin/wsddapi.h   "$EXT_DIR/plugin/" 2>/dev/null || true
        echo "  Copied wsddapi from /usr/share/gsoap/plugin"
    fi
    # Copy all .h files for includes (headers needed by wsddapi)
    if [ -d "/usr/share/gsoap/plugin" ]; then
        cp /usr/share/gsoap/plugin/*.h "$EXT_DIR/plugin/" 2>/dev/null || true
    fi
fi

# Ensure imports exist in external/gsoap/import/
mkdir -p "$EXT_DIR/import"
if [ ! -f "$EXT_DIR/import/wsse.h" ]; then
    echo "[INFO] Copying gSOAP imports to external/gsoap/import/ (best effort)..."
    FOUND_IMPORT_H=$(find /usr /usr/local /opt /home -name "wsse.h" 2>/dev/null | grep -v "external/gsoap" | head -n 1)
    if [ -n "$FOUND_IMPORT_H" ]; then
        IMPORT_SRC=$(dirname "$FOUND_IMPORT_H")
        cp "$IMPORT_SRC"/*.h "$EXT_DIR/import/" 2>/dev/null || true
        echo "  Copied imports from $IMPORT_SRC"
    elif [ -d "/usr/share/gsoap/import" ]; then
        cp /usr/share/gsoap/import/*.h "$EXT_DIR/import/" 2>/dev/null || true
    fi
fi


# Patch wsa5.h: gSOAP 2.8.91's wsa5.h has #import "wsa.h" internally.
# Both define SOAP_ENV__Fault → soapcpp2 semantic error.
# Copy wsa5.h to our import dir (independent of wsse.h check above) then patch it.
# Our copy takes priority because -I "$EXT_DIR/import" is listed first in soapcpp2.
if [ ! -f "$EXT_DIR/import/wsa5.h" ]; then
    FOUND_WSA5=$(find /usr /usr/local /opt -name "wsa5.h" 2>/dev/null | grep -v "external/gsoap" | head -n 1)
    if [ -n "$FOUND_WSA5" ]; then
        cp "$FOUND_WSA5" "$EXT_DIR/import/wsa5.h"
        echo "[INFO] Copied wsa5.h from $FOUND_WSA5"
    elif [ -f "/usr/share/gsoap/import/wsa5.h" ]; then
        cp /usr/share/gsoap/import/wsa5.h "$EXT_DIR/import/wsa5.h"
        echo "[INFO] Copied wsa5.h from /usr/share/gsoap/import"
    fi
fi
if [ -f "$EXT_DIR/import/wsa5.h" ]; then
    sed -i '/#import "wsa\.h"/d' "$EXT_DIR/import/wsa5.h"
    echo "[INFO] Patched wsa5.h: removed #import \"wsa.h\" (prevents SOAP_ENV__Fault clash)"
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

# Fix SOAP_ENV__Fault clash: wsa.h (old) and wsa5.h (2005) both define it.
# We use soapcpp2 -2 (SOAP 1.2), so keep wsa5.h and remove the old wsa.h import.
echo "[INFO] Removing #import \"wsa.h\" to prevent SOAP_ENV__Fault clash with wsa5.h..."
sed -i '/#import "wsa\.h"/d' "$GEN_DIR/onvif.h"

echo "[STEP 2] Patching onvif.h BEFORE soapcpp2 (inject wsse.h import)..."
bash "$SCRIPT_DIR/patch_generated.sh"

echo "[STEP 3] soapcpp2: header → serializers + stubs..."
GSOAP_SYS_PARENT=$(dirname "$GSOAP_SYS_IMPORT")
cd "$GEN_DIR"
soapcpp2 \
    -2 \
    -j \
    -x \
    -I "$EXT_DIR/import" \
    -I "$GSOAP_SYS_IMPORT" \
    -I "$GSOAP_SYS_PARENT" \
    -d "$GEN_DIR" \
    "$GEN_DIR/onvif.h" || true

# soapcpp2 exits 255 on "semantic errors" that are harmless duplicates
# (e.g. SOAP_ENV__Fault redeclared across wsa.h/wsa5.h).
# Verify that the critical output files were actually generated.
if [ ! -f "$GEN_DIR/soapC.cpp" ] || [ ! -f "$GEN_DIR/soapH.h" ]; then
    echo "[ERROR] soapcpp2 failed to generate soapC.cpp / soapH.h — aborting."
    exit 1
fi
echo "[INFO] soapcpp2 generated soapC.cpp and soapH.h successfully."

echo "[STEP 3] Generated files:"
ls -lh "$GEN_DIR/"

echo "[DONE] Code generation complete!"
