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
APP_VERSION := 0.3.2
ifeq ($(CPU_PROBE),1)
CPU_BUILD_DIR ?= build/cpu
CPPFLAGS += -DDS_CPU_PROBE -Iruntime
CPU_LIBRARIES := $(CPU_BUILD_DIR)/libds_cpu_probe.a $(CPU_BUILD_DIR)/dynarmic/src/dynarmic/libdynarmic.a $(CPU_BUILD_DIR)/dynarmic/externals/fmt/libfmt.a $(CPU_BUILD_DIR)/dynarmic/externals/mcl/src/libmcl.a
LIBS := -Wl,--start-group $(CPU_LIBRARIES) -Wl,--end-group $(LIBS)
endif
CPPFLAGS += -DDS_APP_VERSION='"$(APP_VERSION)"'
MAIN_SOURCE := main
ifeq ($(GAME_LAUNCHER),1)
ifneq ($(CPU_PROBE),1)
$(error GAME_LAUNCHER requires CPU_PROBE=1)
endif
MAIN_SOURCE := rebuild_main
CPPFLAGS += -DDS_GAME_LAUNCHER
endif
ifneq ($(strip $(ROMFS_ROOT)),)
ROMFS_OPTION := --romfsdir="$(ROMFS_ROOT)"
endif
OBJECTS := build/$(MAIN_SOURCE).o build/preflight.o build/platform_probe.o build/fixed_function_probe.o build/game_graphics_probe.o build/PVRTDecompress.o

.PHONY: all clean host-test FORCE
FORCE:
all: build/deadspace-switch.nro
build:
	mkdir -p build
build/build-config: FORCE | build
	@printf '%s\n' '$(APP_VERSION)|$(CPU_PROBE)|$(GAME_LAUNCHER)|$(CPU_BUILD_DIR)' > $@.tmp
	@if ! cmp -s $@.tmp $@; then cp $@.tmp $@; fi
	@rm -f $@.tmp
$(OBJECTS) build/deadspace-switch.nacp: build/build-config
build/%.o: source/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/PVRTDecompress.o: ../third_party/powervr/PVRTDecompress.cpp | build
	$(CXX) $(CPPFLAGS) $(filter-out -Werror,$(CXXFLAGS)) -c $< -o $@
build/deadspace-switch.elf: $(OBJECTS) $(CPU_LIBRARIES)
	$(CXX) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@
build/deadspace-switch.nacp: Makefile | build
	nacptool --create "$(APP_TITLE)" "Switch Port Contributors" "$(APP_VERSION)" $@
build/deadspace-switch.nro: build/deadspace-switch.elf build/deadspace-switch.nacp $(APP_ICON) FORCE
	elf2nro $< $@ --nacp=build/deadspace-switch.nacp --icon=$(APP_ICON) $(ROMFS_OPTION)
host-test:
	python3 tests/run_tests.py
clean:
	rm -rf build
-include $(OBJECTS:.o=.d)
