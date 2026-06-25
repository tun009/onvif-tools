#!/bin/bash
# scripts/download_schemas.sh
set -e

SCHEMA_DIR="$(cd "$(dirname "$0")/../../../" && pwd)/ver10/schema"
mkdir -p "$SCHEMA_DIR"
cd "$SCHEMA_DIR"

echo "[INFO] Downloading XSD schema files to $SCHEMA_DIR..."

FILES=(
    "onvif.xsd|https://www.onvif.org/onvif/ver10/schema/onvif.xsd"
    "b-2.xsd|https://docs.oasis-open.org/wsn/b-2.xsd"
    "bf-2.xsd|https://docs.oasis-open.org/wsrf/bf-2.xsd"
    "t-1.xsd|https://docs.oasis-open.org/wsn/t-1.xsd"
    "ws-addr.xsd|https://www.w3.org/2005/08/addressing/ws-addr.xsd"
    "xml.xsd|https://www.w3.org/2001/xml.xsd"
    "xmlmime.xsd|https://www.w3.org/2005/05/xmlmime"
    "xop-include.xsd|https://www.w3.org/2004/08/xop/include"
    "soap-envelope.xsd|https://www.w3.org/2003/05/soap-envelope"
)

for item in "${FILES[@]}"; do
    filename="${item%%|*}"
    url="${item##*|}"
    
    if [ -f "$filename" ]; then
        echo "  SKIP $filename (already exists)"
    else
        echo "  GET  $filename from $url"
        wget -q --no-check-certificate -O "$filename" "$url" || {
            # Try HTTP if HTTPS fails
            http_url=$(echo "$url" | sed 's/https:/http:/')
            echo "  RETRY GET $filename from $http_url"
            wget -q --no-check-certificate -O "$filename" "$http_url" || {
                echo "[WARN] Failed to download $filename, continuing anyway..."
            }
        }
    fi
done

echo "[INFO] Patching local XSD schemas to use offline relative paths..."

# 1. Patch onvif.xsd
if [ -f "onvif.xsd" ]; then
    sed -i 's|http://docs.oasis-open.org/wsn/b-2.xsd|b-2.xsd|g' onvif.xsd
    sed -i 's|https://docs.oasis-open.org/wsn/b-2.xsd|b-2.xsd|g' onvif.xsd
    sed -i 's|http://www.w3.org/2005/08/addressing/ws-addr.xsd|ws-addr.xsd|g' onvif.xsd
    sed -i 's|https://www.w3.org/2005/08/addressing/ws-addr.xsd|ws-addr.xsd|g' onvif.xsd
    sed -i 's|http://www.w3.org/2001/xml.xsd|xml.xsd|g' onvif.xsd
    sed -i 's|https://www.w3.org/2001/xml.xsd|xml.xsd|g' onvif.xsd
    sed -i 's|http://www.w3.org/2005/05/xmlmime|xmlmime.xsd|g' onvif.xsd
    sed -i 's|https://www.w3.org/2005/05/xmlmime|xmlmime.xsd|g' onvif.xsd
    sed -i 's|http://www.w3.org/2004/08/xop/include|xop-include.xsd|g' onvif.xsd
    sed -i 's|https://www.w3.org/2004/08/xop/include|xop-include.xsd|g' onvif.xsd
    sed -i 's|http://www.w3.org/2003/05/soap-envelope|soap-envelope.xsd|g' onvif.xsd
    sed -i 's|https://www.w3.org/2003/05/soap-envelope|soap-envelope.xsd|g' onvif.xsd
fi

# 2. Patch b-2.xsd
if [ -f "b-2.xsd" ]; then
    sed -i 's|http://docs.oasis-open.org/wsrf/bf-2.xsd|bf-2.xsd|g' b-2.xsd
    sed -i 's|https://docs.oasis-open.org/wsrf/bf-2.xsd|bf-2.xsd|g' b-2.xsd
    sed -i 's|http://docs.oasis-open.org/wsn/t-1.xsd|t-1.xsd|g' b-2.xsd
    sed -i 's|https://docs.oasis-open.org/wsn/t-1.xsd|t-1.xsd|g' b-2.xsd
    sed -i 's|http://www.w3.org/2005/08/addressing/ws-addr.xsd|ws-addr.xsd|g' b-2.xsd
    sed -i 's|https://www.w3.org/2005/08/addressing/ws-addr.xsd|ws-addr.xsd|g' b-2.xsd
fi

# 3. Patch bf-2.xsd
if [ -f "bf-2.xsd" ]; then
    sed -i 's|http://www.w3.org/2005/08/addressing/ws-addr.xsd|ws-addr.xsd|g' bf-2.xsd
    sed -i 's|https://www.w3.org/2005/08/addressing/ws-addr.xsd|ws-addr.xsd|g' bf-2.xsd
    sed -i 's|http://www.w3.org/2001/xml.xsd|xml.xsd|g' bf-2.xsd
    sed -i 's|https://www.w3.org/2001/xml.xsd|xml.xsd|g' bf-2.xsd
fi

# 4. Patch soap-envelope.xsd
if [ -f "soap-envelope.xsd" ]; then
    sed -i 's|http://www.w3.org/2001/xml.xsd|xml.xsd|g' soap-envelope.xsd
    sed -i 's|https://www.w3.org/2001/xml.xsd|xml.xsd|g' soap-envelope.xsd
fi

echo "[DONE] Offline schemas download and patching complete!"
