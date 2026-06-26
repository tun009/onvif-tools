CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O0 -pthread -Wno-unused-parameter

INC_DIR   := include
SRC_DIR   := src
GEN_DIR   := generated
EXT_DIR   := external
BUILD_DIR := build

# ── Target 1: ipc-test (no gSOAP needed - for early testing) ─────────────
IPC_BIN  := onvif-ipc-test
IPC_SRCS := $(SRC_DIR)/main.cpp \
            $(SRC_DIR)/backend/BackendConnector.cpp
IPC_OBJS := $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(IPC_SRCS))

# ── Target 2: full ONVIF server (requires: make gen first) ───────────────
FULL_BIN  := onvif-server
APP_SRCS  := $(shell find $(SRC_DIR) -name '*.cpp')
GEN_SRCS  := $(GEN_DIR)/soapC.cpp \
             $(GEN_DIR)/soapDeviceBindingService.cpp \
             $(wildcard $(GEN_DIR)/soapMedia2BindingService.cpp)

# Detect where stdsoap2.cpp is. If it's not in EXT_DIR, we use system libgsoap++
STDSOAP_SRC := $(wildcard $(EXT_DIR)/gsoap/stdsoap2.cpp)
ifeq ($(STDSOAP_SRC),)
  # Use system library
  GSOAP_LIBS := -lgsoapssl++
  GSP_SRCS :=
else
  # Use local source
  GSOAP_LIBS :=
  GSP_SRCS := $(EXT_DIR)/gsoap/stdsoap2.cpp
endif

# wsddapi.cpp (WS-Discovery) — include if present
# wsseapi.cpp is intentionally excluded: it requires XML-DSIG/XML-Enc types
# that conflict with ONVIF-generated types. WS-Security UsernameToken is
# handled directly in WsSecurityHandler.cpp via OpenSSL without wsseapi.
WSDD_SRC := $(wildcard $(EXT_DIR)/gsoap/plugin/wsddapi.cpp)
GSP_SRCS += $(WSDD_SRC)

# Detect any custom serializers copied to external/gsoap/custom (e.g. struct_timeval)
CUSTOM_SRCS := $(wildcard $(EXT_DIR)/gsoap/custom/*.cpp) $(wildcard $(EXT_DIR)/gsoap/custom/*.c)

FULL_SRCS := $(APP_SRCS) $(GEN_SRCS) $(GSP_SRCS) $(CUSTOM_SRCS)
FULL_OBJS := $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(patsubst %.c, $(BUILD_DIR)/%.o, $(FULL_SRCS)))

# ── Includes ──────────────────────────────────────────────────────────────
INCLUDES := \
    -I$(INC_DIR) \
    -I$(GEN_DIR) \
    -I$(EXT_DIR)/gsoap \
    -I$(EXT_DIR)/gsoap/plugin \
    -I$(EXT_DIR)/gsoap/import \
    -I/usr/share/gsoap/import \
    -I/usr/share/gsoap/plugin

# ── pkg-config ────────────────────────────────────────────────────────────
PKG_DEPS   := gstreamer-1.0 gstreamer-rtsp-server-1.0 openssl
PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_DEPS) 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs   $(PKG_DEPS) 2>/dev/null)

DEFINES := -DWITH_OPENSSL -DWITH_DOM -DONVIF_PROFILE_T \
           -DWITH_NONAMESPACES -DWITH_DEFAULT_VIRTUAL

.PHONY: all ipc-test full gen gen-wsdl install_deps clean clean-gen

# Default: build ipc-test first (no gSOAP dependency)
all: ipc-test

# ── IPC test binary (no gSOAP) ────────────────────────────────────────────
ipc-test: $(IPC_OBJS)
	@echo "[LINK] $(IPC_BIN)"
	$(CXX) $(CXXFLAGS) $^ -o $(IPC_BIN) -lpthread
	@echo "[DONE] $(IPC_BIN) - connect to mock backend"

# ── Full ONVIF server (needs generated gSOAP files) ───────────────────────
full: check-gen $(FULL_OBJS)
	@echo "[LINK] $(FULL_BIN)"
	$(CXX) $(CXXFLAGS) $(FULL_OBJS) -o $(FULL_BIN) \
	    $(PKG_LIBS) $(GSOAP_LIBS) -lpthread -lssl -lcrypto -lz
	@echo "[DONE] $(FULL_BIN)"

check-gen:
	@test -f $(GEN_DIR)/soapC.cpp || { \
	    echo "[ERROR] Generated files not found. Run: make gen"; exit 1; }

# ── Compile rules ─────────────────────────────────────────────────────────
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(PKG_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(PKG_CFLAGS) -c $< -o $@

# ── gSOAP code generation ─────────────────────────────────────────────────
gen: gen-wsdl
	bash scripts/gen_gsoap.sh

gen-wsdl:
	bash scripts/download_wsdls.sh

# ── Install deps ──────────────────────────────────────────────────────────
install_deps:
	bash scripts/install_deps.sh

# ── Clean ─────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR) $(IPC_BIN) $(FULL_BIN)

clean-gen:
	rm -rf $(GEN_DIR)/*.cpp $(GEN_DIR)/*.h $(GEN_DIR)/*.nsmap
	rm -f $(EXT_DIR)/gsoap/import/wsa5.h

distclean: clean clean-gen
	rm -rf wsdl/
