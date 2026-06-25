#!/bin/bash
# scripts/gen_device_only.sh
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
    echo "[INFO] Copying stdsoap2.cpp and stdsoap2.h to external/gsoap/..."
    
    # Try dynamic find excluding external/gsoap in case it's custom installed
    FOUND_CPP=$(find /usr /usr/local /opt /home -name "stdsoap2.cpp" 2>/dev/null | grep -v "external/gsoap" | head -n 1)
    if [ -n "$FOUND_CPP" ] && [ -f "$FOUND_CPP" ]; then
        GSOAP_SRC=$(dirname "$FOUND_CPP")
        if [ -f "$GSOAP_SRC/stdsoap2.h" ]; then
            cp "$GSOAP_SRC/stdsoap2.h"   "$EXT_DIR/"
            cp "$GSOAP_SRC/stdsoap2.cpp" "$EXT_DIR/"
            echo "  Copied stdsoap2 files from $GSOAP_SRC"
        fi
    fi
    
    if [ ! -f "$EXT_DIR/stdsoap2.cpp" ]; then
        # Try finding via dpkg
        GSOAP_SRC=$(dpkg -L libgsoap-dev 2>/dev/null | grep stdsoap2.h | head -1 | xargs dirname)
        if [ -n "$GSOAP_SRC" ] && [ -f "$GSOAP_SRC/stdsoap2.cpp" ]; then
            cp "$GSOAP_SRC/stdsoap2.h"   "$EXT_DIR/"
            cp "$GSOAP_SRC/stdsoap2.cpp" "$EXT_DIR/"
            echo "  Copied from dpkg package path: $GSOAP_SRC"
        fi
    fi

    if [ ! -f "$EXT_DIR/stdsoap2.cpp" ]; then
        echo "[ERROR] Cannot find stdsoap2.cpp or stdsoap2.h on the system."
        echo "        Please install libgsoap-dev: sudo apt-get install libgsoap-dev"
        exit 1
    fi
fi

# Ensure plugins exist in external/gsoap/plugin/
mkdir -p "$EXT_DIR/plugin"
if [ ! -f "$EXT_DIR/plugin/wsseapi.cpp" ] || [ ! -f "$EXT_DIR/plugin/wsddapi.cpp" ]; then
    echo "[INFO] Copying gSOAP plugins to external/gsoap/plugin/..."
    FOUND_PLUGIN_CPP=$(find /usr /usr/local /opt /home -name "wsseapi.cpp" 2>/dev/null | grep -v "external/gsoap" | head -n 1)
    if [ -n "$FOUND_PLUGIN_CPP" ]; then
        PLUGIN_SRC=$(dirname "$FOUND_PLUGIN_CPP")
        cp "$PLUGIN_SRC"/*.h "$EXT_DIR/plugin/" 2>/dev/null || true
        cp "$PLUGIN_SRC"/*.cpp "$EXT_DIR/plugin/" 2>/dev/null || true
        echo "  Copied plugins from $PLUGIN_SRC"
    elif [ -d "/usr/share/gsoap/plugin" ]; then
        cp /usr/share/gsoap/plugin/*.h "$EXT_DIR/plugin/" 2>/dev/null || true
        cp /usr/share/gsoap/plugin/*.cpp "$EXT_DIR/plugin/" 2>/dev/null || true
    fi
fi

# Ensure imports exist in external/gsoap/import/
mkdir -p "$EXT_DIR/import"
if [ ! -f "$EXT_DIR/import/wsse.h" ]; then
    echo "[INFO] Copying gSOAP imports to external/gsoap/import/..."
    FOUND_IMPORT_H=$(find /usr /usr/local /opt /home -name "wsse.h" 2>/dev/null | grep -v "external/gsoap" | head -n 1)
    if [ -n "$FOUND_IMPORT_H" ]; then
        IMPORT_SRC=$(dirname "$FOUND_IMPORT_H")
        cp "$IMPORT_SRC"/*.h "$EXT_DIR/import/" 2>/dev/null || true
        echo "  Copied imports from $IMPORT_SRC"
    elif [ -d "/usr/share/gsoap/import" ]; then
        cp /usr/share/gsoap/import/*.h "$EXT_DIR/import/" 2>/dev/null || true
    fi
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

echo "[STEP 1] wsdl2h: devicemgmt.wsdl → C++ header..."
wsdl2h \
    -o "$GEN_DIR/onvif.h" \
    -t "$EXT_DIR/typemap.dat" \
    -I "$GSOAP_SYS_IMPORT" \
    -I "$ROOT_DIR/../../ver10/schema" \
    -I "$WSDL_DIR" \
    -c++11 \
    -j \
    -x \
    "$WSDL_DIR/devicemgmt.wsdl"

echo "[STEP 1] Done: $GEN_DIR/onvif.h ($(wc -l < $GEN_DIR/onvif.h) lines)"

# Fix missing xsi.h issue: xsi namespace is built-in to gSOAP, and its header doesn't exist
# in the standard import directory. Removing the #import statement avoids the compiler error.
if grep -q '#import "xsi.h"' "$GEN_DIR/onvif.h"; then
    echo "[INFO] Removing #import \"xsi.h\" from onvif.h to prevent compilation error..."
    sed -i '/#import "xsi.h"/d' "$GEN_DIR/onvif.h"
fi

# Fix missing ns*.h issue: wsdl2h generates dummy #import "nsX.h" statements for namespaces
# that do not have explicit typemap mappings. Removing them prevents soapcpp2 from failing.
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
