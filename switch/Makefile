# Standalone Switch platform probe. Upstream's ARM32 game loader is NOT linked.
.DEFAULT_GOAL := all
ifndef DEVKITPRO
$(error Set DEVKITPRO to your devkitPro installation)
endif
export PATH := $(DEVKITPRO)/devkitA64/bin:$(DEVKITPRO)/tools/bin:$(PATH)
CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++
PORT := $(DEVKITPRO)/portlibs/switch
NX := $(DEVKITPRO)/libnx
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec
CPPFLAGS := -D__SWITCH__ -D_REENTRANT -Iinclude -isystem $(NX)/include -isystem $(PORT)/include -I../third_party/powervr
CXXFLAGS := -std=gnu++17 -O2 -g -Wall -Wextra -Werror $(ARCH) -fPIC -ffunction-sections -fdata-sections
LDFLAGS := $(ARCH) -fPIC -specs=$(NX)/switch.specs -Wl,-Map,build/deadspace-switch.map -Wl,--gc-sections -L$(NX)/lib -L$(PORT)/lib
LIBS := -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau -lnx -lm -lpthread
OBJECTS := build/main.o build/preflight.o build/platform_probe.o build/fixed_function_probe.o build/PVRTDecompress.o

.PHONY: all clean host-test
all: build/deadspace-switch.nro
build:
	mkdir -p build
build/%.o: source/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/PVRTDecompress.o: ../third_party/powervr/PVRTDecompress.cpp | build
	$(CXX) $(CPPFLAGS) $(filter-out -Werror,$(CXXFLAGS)) -c $< -o $@
build/deadspace-switch.elf: $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@
build/deadspace-switch.nacp: Makefile | build
	nacptool --create "Dead Space Switch Diagnostics" "Switch Port Contributors" "0.1.0" $@
build/deadspace-switch.nro: build/deadspace-switch.elf build/deadspace-switch.nacp
	elf2nro $< $@ --nacp=build/deadspace-switch.nacp --icon=$(NX)/default_icon.jpg
host-test:
	python3 tests/run_tests.py
clean:
	rm -rf build
-include $(OBJECTS:.o=.d)
