#!/bin/bash
set -e
echo "[1] apt packages..."
sudo apt-get update -qq
sudo apt-get install -y \
    build-essential \
    gsoap \
    libgsoap-dev \
    libssl-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstrtspserver-1.0-dev \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    wget

echo "[2] Copy gSOAP runtime files..."
GSOAP_SRC=$(dpkg -L libgsoap-dev 2>/dev/null | grep stdsoap2.h | head -1 | xargs dirname)
if [ -n "$GSOAP_SRC" ]; then
    cp "$GSOAP_SRC/stdsoap2.h"   external/gsoap/ 2>/dev/null || true
    cp "$GSOAP_SRC/stdsoap2.cpp" external/gsoap/ 2>/dev/null || true
    echo "  Copied from $GSOAP_SRC"
else
    echo "  WARN: stdsoap2 not found via dpkg, trying /usr/share/gsoap/src/"
    cp /usr/share/gsoap/src/stdsoap2.h   external/gsoap/ 2>/dev/null || true
    cp /usr/share/gsoap/src/stdsoap2.cpp external/gsoap/ 2>/dev/null || true
fi

echo "[3] Copy gSOAP plugins (wsseapi, wsddapi)..."
PLUGIN_SRC="/usr/share/gsoap/plugin"
[ -d "$PLUGIN_SRC" ] && cp "$PLUGIN_SRC"/*.h "$PLUGIN_SRC"/*.cpp \
    external/gsoap/plugin/ 2>/dev/null || true

IMPORT_SRC="/usr/share/gsoap/import"
[ -d "$IMPORT_SRC" ] && cp "$IMPORT_SRC"/*.h \
    external/gsoap/import/ 2>/dev/null || true

echo "[4] Download WSDLs..."
bash scripts/download_wsdls.sh

echo "[DONE] Run: make gen && make"
