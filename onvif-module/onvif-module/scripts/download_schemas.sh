#!/bin/bash
# scripts/download_schemas.sh
# Downloads all XSD schemas required by devicemgmt.wsdl for offline wsdl2h compilation.

SCHEMA_DIR="$(cd "$(dirname "$0")/../../../" && pwd)/ver10/schema"
mkdir -p "$SCHEMA_DIR"
cd "$SCHEMA_DIR"

echo "[INFO] Downloading XSD schema files to $SCHEMA_DIR..."

# NOTE: URLs must point to actual .xsd files, not HTML pages.
# W3C schema URLs without .xsd extension return HTML and cause namespace mismatch.
FILES=(
    "onvif.xsd|https://www.onvif.org/onvif/ver10/schema/onvif.xsd"
    "common.xsd|https://www.onvif.org/onvif/ver10/schema/common.xsd"
    "b-2.xsd|https://docs.oasis-open.org/wsn/b-2.xsd"
    "bf-2.xsd|https://docs.oasis-open.org/wsrf/bf-2.xsd"
    "t-1.xsd|https://docs.oasis-open.org/wsn/t-1.xsd"
    "ws-addr.xsd|https://www.w3.org/2005/08/addressing/ws-addr.xsd"
    "xml.xsd|https://www.w3.org/2001/xml.xsd"
    "xmlmime.xsd|https://www.w3.org/2005/05/xmlmime.xsd"
    "xop-include.xsd|https://www.w3.org/2004/08/xop/include.xsd"
    "r-2.xsd|https://docs.oasis-open.org/wsrf/r-2.xsd"
    "ws-discovery.xsd|https://schemas.xmlsoap.org/ws/2005/04/discovery/ws-discovery.xsd"
    "addressing.xsd|https://schemas.xmlsoap.org/ws/2004/08/addressing/addressing.xsd"
    "metadatastream.xsd|https://www.onvif.org/onvif/ver10/schema/metadatastream.xsd"
)

for item in "${FILES[@]}"; do
    filename="${item%%|*}"
    url="${item##*|}"

    # Remove bad cached file if it is smaller than 200 bytes (likely HTML error page)
    if [ -f "$filename" ] && [ "$(wc -c < "$filename")" -lt 200 ]; then
        echo "  REMOVE bad cached $filename (too small, likely HTML)"
        rm -f "$filename"
    fi

    if [ -f "$filename" ]; then
        echo "  SKIP $filename (already exists)"
    else
        echo "  GET  $filename"
        wget -q --no-check-certificate -O "$filename" "$url" || {
            http_url=$(echo "$url" | sed 's/https:/http:/')
            echo "  RETRY $filename via HTTP"
            wget -q --no-check-certificate -O "$filename" "$http_url" || {
                echo "  [WARN] Failed to download $filename, skipping..."
                rm -f "$filename"
            }
        }
    fi
done

echo "[INFO] Patching local XSD schemas to use offline relative paths..."

# 1. Patch onvif.xsd
if [ -f "onvif.xsd" ]; then
    sed -i 's|schemaLocation="http://docs.oasis-open.org/wsn/b-2.xsd"|schemaLocation="b-2.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="https://docs.oasis-open.org/wsn/b-2.xsd"|schemaLocation="b-2.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="http://www.w3.org/2005/08/addressing/ws-addr.xsd"|schemaLocation="ws-addr.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="https://www.w3.org/2005/08/addressing/ws-addr.xsd"|schemaLocation="ws-addr.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="http://www.w3.org/2005/05/xmlmime"|schemaLocation="xmlmime.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="https://www.w3.org/2005/05/xmlmime"|schemaLocation="xmlmime.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="https://www.w3.org/2005/05/xmlmime.xsd"|schemaLocation="xmlmime.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="http://www.w3.org/2004/08/xop/include"|schemaLocation="xop-include.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="https://www.w3.org/2004/08/xop/include"|schemaLocation="xop-include.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="https://www.w3.org/2004/08/xop/include.xsd"|schemaLocation="xop-include.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="http://www.w3.org/2003/05/soap-envelope"|schemaLocation="soap-envelope.xsd"|g' onvif.xsd
    sed -i 's|schemaLocation="https://www.w3.org/2003/05/soap-envelope"|schemaLocation="soap-envelope.xsd"|g' onvif.xsd
    echo "  patched onvif.xsd"
fi

# 2. Patch b-2.xsd
if [ -f "b-2.xsd" ]; then
    sed -i 's|schemaLocation="http://docs.oasis-open.org/wsrf/bf-2.xsd"|schemaLocation="bf-2.xsd"|g' b-2.xsd
    sed -i 's|schemaLocation="https://docs.oasis-open.org/wsrf/bf-2.xsd"|schemaLocation="bf-2.xsd"|g' b-2.xsd
    sed -i 's|schemaLocation="http://docs.oasis-open.org/wsn/t-1.xsd"|schemaLocation="t-1.xsd"|g' b-2.xsd
    sed -i 's|schemaLocation="https://docs.oasis-open.org/wsn/t-1.xsd"|schemaLocation="t-1.xsd"|g' b-2.xsd
    sed -i 's|schemaLocation="http://www.w3.org/2005/08/addressing/ws-addr.xsd"|schemaLocation="ws-addr.xsd"|g' b-2.xsd
    sed -i 's|schemaLocation="https://www.w3.org/2005/08/addressing/ws-addr.xsd"|schemaLocation="ws-addr.xsd"|g' b-2.xsd
    echo "  patched b-2.xsd"
fi

# 3. Patch bf-2.xsd
if [ -f "bf-2.xsd" ]; then
    sed -i 's|schemaLocation="http://www.w3.org/2005/08/addressing/ws-addr.xsd"|schemaLocation="ws-addr.xsd"|g' bf-2.xsd
    sed -i 's|schemaLocation="https://www.w3.org/2005/08/addressing/ws-addr.xsd"|schemaLocation="ws-addr.xsd"|g' bf-2.xsd
    sed -i 's|schemaLocation="http://www.w3.org/2001/xml.xsd"|schemaLocation="xml.xsd"|g' bf-2.xsd
    sed -i 's|schemaLocation="https://www.w3.org/2001/xml.xsd"|schemaLocation="xml.xsd"|g' bf-2.xsd
    echo "  patched bf-2.xsd"
fi

# 4. Patch r-2.xsd
if [ -f "r-2.xsd" ]; then
    sed -i 's|schemaLocation="http://docs.oasis-open.org/wsrf/bf-2.xsd"|schemaLocation="bf-2.xsd"|g' r-2.xsd
    sed -i 's|schemaLocation="https://docs.oasis-open.org/wsrf/bf-2.xsd"|schemaLocation="bf-2.xsd"|g' r-2.xsd
    echo "  patched r-2.xsd"
fi

# 5. Patch ws-discovery.xsd
if [ -f "ws-discovery.xsd" ]; then
    sed -i 's|schemaLocation="http://schemas.xmlsoap.org/ws/2004/08/addressing"|schemaLocation="addressing.xsd"|g' ws-discovery.xsd
    sed -i 's|schemaLocation="https://schemas.xmlsoap.org/ws/2004/08/addressing"|schemaLocation="addressing.xsd"|g' ws-discovery.xsd
    echo "  patched ws-discovery.xsd"
fi

# 6. Patch metadatastream.xsd
if [ -f "metadatastream.xsd" ]; then
    sed -i 's|schemaLocation="http://docs.oasis-open.org/wsn/b-2.xsd"|schemaLocation="b-2.xsd"|g' metadatastream.xsd
    sed -i 's|schemaLocation="https://docs.oasis-open.org/wsn/b-2.xsd"|schemaLocation="b-2.xsd"|g' metadatastream.xsd
    echo "  patched metadatastream.xsd"
fi

# 7. Download ver20 analytics schemas
VER20_ANALYTICS_DIR="$(cd "$(dirname "$0")/../../../" && pwd)/ver20/analytics"
mkdir -p "$VER20_ANALYTICS_DIR"
echo "[INFO] Downloading ver20 analytics schemas..."
wget -q --no-check-certificate -O "$VER20_ANALYTICS_DIR/humanface.xsd" "https://www.onvif.org/onvif/ver20/analytics/humanface.xsd" || true
wget -q --no-check-certificate -O "$VER20_ANALYTICS_DIR/humanbody.xsd" "https://www.onvif.org/onvif/ver20/analytics/humanbody.xsd" || true

echo ""
echo "[DONE] Offline schemas download and patching complete!"
echo "Files in $SCHEMA_DIR:"
ls -lh "$SCHEMA_DIR/"
