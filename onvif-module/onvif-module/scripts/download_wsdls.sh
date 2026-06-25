#!/bin/bash
# scripts/download_wsdls.sh
set -e

WSDL_DIR="$(cd "$(dirname "$0")/.." && pwd)/wsdl"
mkdir -p "$WSDL_DIR"

WSDLS=(
    "https://www.onvif.org/onvif/ver10/device/wsdl/devicemgmt.wsdl"
    "https://www.onvif.org/onvif/ver20/media/wsdl/media.wsdl"
    "https://www.onvif.org/onvif/ver20/ptz/wsdl/ptz.wsdl"
    "https://www.onvif.org/onvif/ver20/imaging/wsdl/imaging.wsdl"
    "https://www.onvif.org/onvif/ver10/events/wsdl/event.wsdl"
    "https://www.onvif.org/onvif/ver20/analytics/wsdl/analytics.wsdl"
    "https://www.onvif.org/onvif/ver10/network/wsdl/remotediscovery.wsdl"
    "https://docs.oasis-open.org/wsn/bw-2.wsdl"
    "https://docs.oasis-open.org/wsrf/rw-2.wsdl"
)

echo "[INFO] Downloading ONVIF WSDLs..."
for url in "${WSDLS[@]}"; do
    fname=$(basename "$url")
    if [ -f "$WSDL_DIR/$fname" ]; then
        echo "  SKIP $fname (cached)"
    else
        echo "  GET  $fname"
        wget -q -P "$WSDL_DIR" "$url" || echo "  WARN: failed $url"
    fi
done

echo "[DONE] WSDLs in $WSDL_DIR:"
ls "$WSDL_DIR/"
