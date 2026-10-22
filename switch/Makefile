# Switch diagnostics; CPU_PROBE links the ARM32 runtime and native GL subset.
.DEFAULT_GOAL := all
ifndef DEVKITPRO
$(error Set DEVKITPRO to your devkitPro installation)
endif
export PATH := $(DEVKITPRO)/devkitA64/bin:$(DEVKITPRO)/tools/bin:$(PATH)
CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++
PORT := $(DEVKITPRO)/portlibs/switch
NX := $(DEVKITPRO)/libnx
APP_TITLE := Dead Space Mobile
APP_ICON := assets/icon.jpg
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec
CPPFLAGS := -D__SWITCH__ -D_REENTRANT -Iinclude -isystem $(NX)/include -isystem $(PORT)/include -I../third_party/powervr
CXXFLAGS := -std=gnu++17 -O2 -g -Wall -Wextra -Werror $(ARCH) -fPIC -ffunction-sections -fdata-sections
LDFLAGS := $(ARCH) -fPIC -specs=$(NX)/switch.specs -Wl,-Map,build/deadspace-switch.map -Wl,--gc-sections -L$(NX)/lib -L$(PORT)/lib
LIBS := -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau -lnx -lm -lpthread
APP_VERSION := 0.3.6
ENTRY := main
ifeq ($(STANDALONE),1)
ifneq ($(CPU_PROBE),1)
$(error STANDALONE=1 requires CPU_PROBE=1)
endif
ENTRY := standalone
endif
ifneq ($(strip $(TITLE_ID)),)
NACP_OPTIONS := --titleid=$(TITLE_ID)
endif
ifeq ($(CPU_PROBE),1)
CPU_BUILD_DIR ?= build/cpu
CPPFLAGS += -DDS_CPU_PROBE -Iruntime
CPU_LIBS := $(CPU_BUILD_DIR)/libds_cpu_probe.a $(CPU_BUILD_DIR)/dynarmic/src/dynarmic/libdynarmic.a $(CPU_BUILD_DIR)/dynarmic/externals/fmt/libfmt.a $(CPU_BUILD_DIR)/dynarmic/externals/mcl/src/libmcl.a
LIBS := -Wl,--start-group $(CPU_LIBS) -Wl,--end-group $(LIBS)
endif
CPPFLAGS += -DDS_APP_VERSION='"$(APP_VERSION)"'
ifneq ($(strip $(ROMFS_ROOT)),)
ROMFS_OPTION := --romfsdir="$(ROMFS_ROOT)"
endif
OBJECTS := build/$(ENTRY).o build/preflight.o build/platform_probe.o build/fixed_function_probe.o build/game_graphics_probe.o build/PVRTDecompress.o

.PHONY: all clean host-test FORCE
FORCE:
all: build/deadspace-switch.nro
build:
	mkdir -p build
# Invalidate objects when switching CPU/platform or standalone/diagnostics.
# Updating the stamp only when its contents change preserves incremental builds.
build/config: FORCE | build
	@printf '%s\n' 'CPU_PROBE=$(CPU_PROBE)' 'STANDALONE=$(STANDALONE)' 'CPU_BUILD_DIR=$(CPU_BUILD_DIR)' 'APP_VERSION=$(APP_VERSION)' 'TITLE_ID=$(TITLE_ID)' > $@.tmp
	@cmp -s $@.tmp $@ || cp $@.tmp $@
	@rm -f $@.tmp
build/%.o: source/%.cpp build/config | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/PVRTDecompress.o: ../third_party/powervr/PVRTDecompress.cpp | build
	$(CXX) $(CPPFLAGS) $(filter-out -Werror,$(CXXFLAGS)) -c $< -o $@
build/deadspace-switch.elf: $(OBJECTS) $(CPU_LIBS)
	$(CXX) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@
build/deadspace-switch.nso: build/deadspace-switch.elf
	elf2nso $< $@
build/deadspace-switch.nacp: Makefile build/config | build
	nacptool --create "$(APP_TITLE)" "Xiao Fan" "$(APP_VERSION)" $@ $(NACP_OPTIONS)
build/deadspace-switch.nro: build/deadspace-switch.elf build/deadspace-switch.nacp $(APP_ICON) FORCE
	elf2nro $< $@ --nacp=build/deadspace-switch.nacp --icon=$(APP_ICON) $(ROMFS_OPTION)
host-test:
	python3 tests/run_tests.py
clean:
	rm -rf build
-include $(OBJECTS:.o=.d)
