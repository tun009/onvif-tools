CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread -Wno-unused-parameter

SRC_DIR   := src
INC_DIR   := include
BUILD_DIR := build
BIN       := mock-camera-server

SRCS := $(shell find $(SRC_DIR) -name '*.cpp')
OBJS := $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(SRCS))

INCLUDES := -I$(INC_DIR) -I$(INC_DIR)/third_party

PKG_DEPS   :=
PKG_CFLAGS :=
PKG_LIBS   :=

LIBS := -lpthread

.PHONY: all clean install_deps

all: $(BIN)

$(BIN): $(OBJS)
	@echo "[LINK] $@"
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)
	@echo "[DONE] $@"

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(PKG_CFLAGS) -c $< -o $@

install_deps:
	bash scripts/install_deps.sh

clean:
	rm -rf $(BUILD_DIR) $(BIN)
