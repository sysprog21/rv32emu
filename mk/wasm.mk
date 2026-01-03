# WebAssembly build configuration
#
# Provides Emscripten-specific build flags and targets.

ifndef _MK_WASM_INCLUDED
_MK_WASM_INCLUDED := 1

CFLAGS_emcc ?=
deps_emcc :=
ASSETS := assets/wasm
WEB_HTML_RESOURCES := $(ASSETS)/html
WEB_JS_RESOURCES := $(ASSETS)/js
EXPORTED_FUNCS := _main,_indirect_rv_halt,_get_input_buf,_get_input_buf_cap,_set_input_buf_size
DEMO_DIR := demo

WEB_FILES := $(BIN).js \
             $(BIN).wasm \
             $(BIN).worker.js \
             $(OUT)/elf_list.js

# Only configure Emscripten settings when using emcc
ifeq ("$(CC_IS_EMCC)", "1")

BIN := $(BIN).js

# Tail-call optimization
CFLAGS += -mtail-call

# SDL configuration for Emscripten
ifeq ($(CONFIG_SDL),y)
# Disable STRICT mode to avoid -Werror in SDL2_mixer port compilation
CFLAGS_emcc += -sSTRICT=0 -sUSE_SDL=2 -sSDL2_MIXER_FORMATS=wav,mid -sUSE_SDL_MIXER=2
OBJS_EXT += syscall_sdl.o
LDFLAGS += -pthread
endif

# Emscripten build flags
CFLAGS_emcc += -sINITIAL_MEMORY=2GB \
               -sALLOW_MEMORY_GROWTH \
               -s"EXPORTED_FUNCTIONS=$(EXPORTED_FUNCS)" \
               -sSTACK_SIZE=4MB \
               -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency \
               --embed-file build/timidity@/etc/timidity \
               -DMEM_SIZE=0x20000000 \
               -DCYCLE_PER_STEP=2000000 \
               -O3 \
               -w

# System mode assets
ifeq ($(CONFIG_SYSTEM),y)
CFLAGS_emcc += --embed-file build/linux-image/Image@Image \
               --embed-file build/linux-image/rootfs.cpio@rootfs.cpio \
               --embed-file build/minimal.dtb@/minimal.dtb \
               --pre-js $(WEB_JS_RESOURCES)/system-pre.js
else
CFLAGS_emcc += --embed-file build/jit-bf.elf@/jit-bf.elf \
               --embed-file build/coro.elf@/coro.elf \
               --embed-file build/fibonacci.elf@/fibonacci.elf \
               --embed-file build/hello.elf@/hello.elf \
               --embed-file build/ieee754.elf@/ieee754.elf \
               --embed-file build/perfcount.elf@/perfcount.elf \
               --embed-file build/readelf.elf@/readelf.elf \
               --embed-file build/smolnes.elf@/smolnes.elf \
               --embed-file build/riscv32@/riscv32 \
               --embed-file build/DOOM1.WAD@/DOOM1.WAD \
               --embed-file build/id1/pak0.pak@/id1/pak0.pak \
               --pre-js $(WEB_JS_RESOURCES)/user-pre.js
endif

# mimalloc support detection
MIMALLOC_SUPPORT_SINCE_MAJOR := 3
MIMALLOC_SUPPORT_SINCE_MINOR := 1
MIMALLOC_SUPPORT_SINCE_PATCH := 50
ifeq ($(call version_gte,$(EMCC_MAJOR),$(EMCC_MINOR),$(EMCC_PATCH),$(MIMALLOC_SUPPORT_SINCE_MAJOR),$(MIMALLOC_SUPPORT_SINCE_MINOR),$(MIMALLOC_SUPPORT_SINCE_PATCH)), 1)
    CFLAGS_emcc += -sMALLOC=mimalloc
else
    $(warning mimalloc requires Emscripten $(MIMALLOC_SUPPORT_SINCE_MAJOR).$(MIMALLOC_SUPPORT_SINCE_MINOR).$(MIMALLOC_SUPPORT_SINCE_PATCH)+)
endif

# ELF list generator
$(OUT)/elf_list.js: artifact tools/gen-elf-list-js.py
	$(Q)tools/gen-elf-list-js.py > $@

# Dependencies for WASM build
# System mode only needs artifact (linux-image) and timidity (audio)
# User mode needs elf_list.js for demo selection and game data for embedding
ifeq ($(CONFIG_SYSTEM),y)
deps_emcc += artifact $(TIMIDITY_DATA)
else
deps_emcc += artifact $(OUT)/elf_list.js $(DOOM_DATA) $(QUAKE_DATA) $(TIMIDITY_DATA)
endif

# Browser TCO Support Detection

CHROME_SUPPORT_TCO_AT_MAJOR := 112
FIREFOX_SUPPORT_TCO_AT_MAJOR := 121
SAFARI_SUPPORT_TCO_AT_MAJOR := 18
SAFARI_SUPPORT_TCO_AT_MINOR := 2

# Browser detection (platform-specific)
ifeq ($(UNAME_S),Darwin)
    CHROME_MAJOR := $(shell "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --version 2>/dev/null | awk '{print $$3}' | cut -f1 -d.)
    FIREFOX_MAJOR := $(shell /Applications/Firefox.app/Contents/MacOS/firefox --version 2>/dev/null | awk '{print $$3}' | cut -f1 -d.)
    SAFARI_VERSION := $(shell mdls -name kMDItemVersion /Applications/Safari.app 2>/dev/null | sed 's/"//g' | awk '{print $$3}')
else ifeq ($(UNAME_S),Linux)
    CHROME_MAJOR := $(shell google-chrome --version 2>/dev/null | awk '{print $$3}' | cut -f1 -d.)
    FIREFOX_MAJOR := $(shell firefox -v 2>/dev/null | awk '{print $$3}' | cut -f1 -d.)
endif

# Browser support notifications
ifneq ($(CHROME_MAJOR),)
ifeq ($(call version_gte,$(CHROME_MAJOR),,,$(CHROME_SUPPORT_TCO_AT_MAJOR),,), 1)
    $(info $(call noticex, Chrome $(CHROME_MAJOR) supports TCO))
else
    $(warning Chrome $(CHROME_MAJOR) does not support TCO (requires $(CHROME_SUPPORT_TCO_AT_MAJOR)+))
endif
endif

ifneq ($(FIREFOX_MAJOR),)
ifeq ($(call version_gte,$(FIREFOX_MAJOR),,,$(FIREFOX_SUPPORT_TCO_AT_MAJOR),,), 1)
    $(info $(call noticex, Firefox $(FIREFOX_MAJOR) supports TCO))
else
    $(warning Firefox $(FIREFOX_MAJOR) does not support TCO (requires $(FIREFOX_SUPPORT_TCO_AT_MAJOR)+))
endif
endif

# Web Demo Server

DEMO_IP := 127.0.0.1
DEMO_PORT := 8000

check-demo-dir-exist:
	$(Q)if [ ! -d "$(DEMO_DIR)" ]; then mkdir -p "$(DEMO_DIR)"; fi

define cp-web-file
    $(Q)cp $(1) $(DEMO_DIR)
    $(info)
endef

STATIC_WEB_FILES := $(WEB_JS_RESOURCES)/coi-serviceworker.min.js
ifeq ($(CONFIG_SYSTEM),y)
STATIC_WEB_FILES += $(WEB_HTML_RESOURCES)/system.html
else
STATIC_WEB_FILES += $(WEB_HTML_RESOURCES)/user.html
endif

start_web_deps := check-demo-dir-exist $(BIN)
ifeq ($(CONFIG_SYSTEM),y)
start_web_deps += $(BUILD_DTB) $(BUILD_DTB2C)
endif

start-web: $(start_web_deps)
	$(Q)rm -f $(DEMO_DIR)/*.html
	$(foreach T, $(WEB_FILES), $(call cp-web-file, $(T)))
	$(foreach T, $(STATIC_WEB_FILES), $(call cp-web-file, $(T)))
	$(Q)mv $(DEMO_DIR)/*.html $(DEMO_DIR)/index.html
	$(Q)python3 -m http.server --bind $(DEMO_IP) $(DEMO_PORT) --directory $(DEMO_DIR)

.PHONY: check-demo-dir-exist start-web

endif # CC_IS_EMCC

endif # _MK_WASM_INCLUDED
