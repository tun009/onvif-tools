#!/bin/bash
set -e
echo "[1] apt packages..."
sudo apt-get update -qq
sudo apt-get install -y \
    build-essential \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    libgstrtspserver-1.0-dev \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-tools \
    nlohmann-json3-dev

echo "[2] Copy nlohmann/json.hpp..."
mkdir -p include/third_party/nlohmann
cp /usr/include/nlohmann/json.hpp include/third_party/nlohmann/json.hpp \
  || wget -q -O include/third_party/nlohmann/json.hpp \
     https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp

echo "[3] Check Jetson nvv4l2 plugins..."
gst-inspect-1.0 nvv4l2h264enc && echo "nvv4l2h264enc OK" || echo "WARN: not found (JetPack needed)"
gst-inspect-1.0 nvv4l2h265enc && echo "nvv4l2h265enc OK" || echo "WARN: not found (JetPack needed)"

echo "[4] Download MediaMTX..."
MACHINE=$(uname -m)
if [ "$MACHINE" = "x86_64" ]; then
  ARCH="amd64"
elif [ "$MACHINE" = "aarch64" ]; then
  ARCH="arm64v8"
else
  ARCH="arm64v8"
fi
VER="v1.9.1"
mkdir -p bin
wget -q -O /tmp/mediamtx.tar.gz \
  "https://github.com/bluenviron/mediamtx/releases/download/${VER}/mediamtx_${VER}_linux_${ARCH}.tar.gz"
tar -xzf /tmp/mediamtx.tar.gz -C bin/ mediamtx
chmod +x bin/mediamtx
echo "mediamtx installed: bin/mediamtx"

echo "[DONE] All deps installed."
