# onvif-module

ONVIF Profile T server module cho Jetson Orin NX + JetPack 5.

## Quick Start (Phase 1: IPC Test - no gSOAP needed)

```bash
# 1. Install dependencies
bash scripts/install_deps.sh

# 2. Build IPC test binary
make ipc-test

# 3. Start mock backend first (separate terminal)
cd ../mock-camera-backend
./mock-camera-server config/mock.conf &
bash scripts/start_streams.sh

# 4. Run IPC smoke test
./onvif-ipc-test config/onvif.conf
```

Expected output:
```
[TEST] GetDeviceInfo:
  Manufacturer : MockCam Inc.
  Model        : MockCam-4K
[TEST] GetProfiles: 3 profiles
[TEST] StreamUri [profile_main]: rtsp://127.0.0.1:8554/main
...
[TEST] All smoke tests PASSED
```

## Quick Start (Phase 2: Full ONVIF Server)

```bash
# 1. Generate gSOAP code from WSDLs
make gen

# 2. Build full server
make full

# 3. Run
./onvif-server config/onvif.conf
```

## Test with ONVIF Device Manager

1. Install [ONVIF Device Manager](https://sourceforge.net/projects/onvifdm/) on Windows
2. Add device: `http://192.168.1.100:8080/onvif/device`
3. Credentials: admin / admin123

## Services (Profile T)

| Service   | URL                                | Status   |
|-----------|------------------------------------|----------|
| Device    | http://{ip}:8080/onvif/device      | ✅ Done  |
| Media2    | http://{ip}:8080/onvif/media       | 🔄 Next  |
| PTZ       | http://{ip}:8080/onvif/ptz         | 🔄 Next  |
| Imaging   | http://{ip}:8080/onvif/imaging     | 🔄 Next  |
| Events    | http://{ip}:8080/onvif/events      | 🔄 Next  |
| Analytics | http://{ip}:8080/onvif/analytics   | 🔄 Next  |

## Build Targets

| Target      | Command       | Requires gSOAP |
|-------------|---------------|----------------|
| IPC test    | `make`        | No             |
| Full server | `make full`   | Yes (make gen) |
| Gen code    | `make gen`    | wsdl2h/soapcpp2|
| Deps        | `make install_deps` | -        |

## Requirements

- Ubuntu 22.04 + JetPack 5
- g++ 11+ (build-essential)
- gsoap + libgsoap-dev
- libssl-dev
- libgstreamer1.0-dev + libgstrtspserver-1.0-dev
