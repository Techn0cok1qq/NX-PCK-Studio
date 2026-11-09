# NX PCK Studio (Nintendo Switch homebrew) — devkitPro / libnx
ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO が未設定です (devkitPro を入れてください)")
endif
LIBNX ?= $(DEVKITPRO)/libnx
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := nxpckstudio
APP_TITLE   := NX PCK Studio
APP_AUTHOR  := cokacolastic
APP_VERSION := 1.0.0
ROMFS       := romfs

PORTLIBS := $(DEVKITPRO)/portlibs/switch
PKGCONF  := $(PORTLIBS)/bin/aarch64-none-elf-pkg-config
export PKG_CONFIG_PATH := $(PORTLIBS)/lib/pkgconfig

ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CXXFLAGS := -g -O2 -Wall -ffunction-sections $(ARCH) -D__SWITCH__ -std=gnu++17 -fno-rtti -fno-exceptions \
            -I$(LIBNX)/include -I$(PORTLIBS)/include \
            $(shell $(PKGCONF) --cflags sdl2 SDL2_image SDL2_ttf)
LDFLAGS  := -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(TARGET).map
LIBS     := $(shell $(PKGCONF) --libs --static SDL2_image SDL2_ttf sdl2) -lnx

CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++
OBJS := $(patsubst source/%.cpp,build/%.o,$(wildcard source/*.cpp))
NROFLAGS += --nacp=$(TARGET).nacp --romfsdir=$(ROMFS)

# レシピ行の先頭を Tab でなく ">" にする (貼り付けで Tab が空白に化けても動く)
.RECIPEPREFIX := >

.PHONY: all clean
all: $(TARGET).nro

$(TARGET).nacp:
> @nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp $(shell find $(ROMFS) -type f)

$(TARGET).elf: $(OBJS)
> $(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

build/%.o: source/%.cpp $(wildcard source/*.hpp)
> @mkdir -p build
> $(CXX) $(CXXFLAGS) -c $< -o $@

clean:
> rm -rf build $(TARGET).elf $(TARGET).nro $(TARGET).nacp $(TARGET).map
