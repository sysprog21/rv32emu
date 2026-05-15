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
# Base exported functions (always available)
EXPORTED_FUNCS := _main,_indirect_rv_halt,_indirect_rv_alive,_indirect_rv_cleanup
# System mode adds UART input buffer functions
ifeq ($(CONFIG_SYSTEM),y)
EXPORTED_FUNCS := $(EXPORTED_FUNCS),_get_input_buf,_get_input_buf_cap,_set_input_buf_size,_get_input_buf_size,_u8250_put_rx_char
endif
DEMO_DIR_BASE := demo
ifeq ($(CONFIG_SYSTEM),y)
DEMO_DIR := $(DEMO_DIR_BASE)/system
else
DEMO_DIR := $(DEMO_DIR_BASE)/user
endif

# Base web files. User mode also ships the bulky game-data assets that were
# moved out of --embed-file so the WASM binary stays small. They are fetched
# at runtime by user.html and written into MEMFS at the paths the binaries
# expect (/DOOM1.WAD, /id1/pak0.pak, /etc/timidity/*).
WEB_FILES := $(BIN).js \
             $(BIN).wasm
ifneq ($(CONFIG_SYSTEM),y)
WEB_FILES += $(OUT)/elf_list.js \
             $(OUT)/DOOM1.WAD \
             $(OUT)/pak0.pak \
             $(OUT)/timidity.tar \
             $(OUT)/timidity.tar.gz
endif

# Only configure Emscripten settings when using emcc
ifeq ("$(CC_IS_EMCC)", "1")

BIN := $(BIN).js

# Tail-call optimization - requires both compile and link flags
CFLAGS += -mtail-call
LDFLAGS += -mtail-call

# SDL configuration for Emscripten
ifeq ($(CONFIG_SDL),y)
# Disable STRICT mode to avoid -Werror in SDL2_mixer port compilation
CFLAGS_emcc += -sSTRICT=0 -sUSE_SDL=2 -sSDL2_MIXER_FORMATS=wav,mid -sUSE_SDL_MIXER=2
CFLAGS_emcc += -pthread -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency
OBJS_EXT += syscall_sdl.o
LDFLAGS += -pthread
# Note: Emscripten 4.x inlines worker code into the main JS file
endif

# Emscripten build flags
CFLAGS_emcc += -sALLOW_MEMORY_GROWTH \
               -s"EXPORTED_FUNCTIONS=$(EXPORTED_FUNCS)" \
               -sSTACK_SIZE=4MB \
               -DCYCLE_PER_STEP=2000000 \
               -DWASM_BLOCK_LIMIT=5000 \
               -DWASM_BLOCK_HARD_LIMIT=10000 \
               -O3 \
               -w

# System mode: kernel + rootfs fetched on demand, DTB embedded (1.5 KB).
# User mode: small ELFs + game binaries embedded (~4 MiB); large game data
# (DOOM1.WAD, Quake pak0.pak, timidity instrument set) is fetched on demand
# from the same origin and written to MEMFS at the paths the binaries expect.
ifeq ($(CONFIG_SYSTEM),y)
CFLAGS_emcc += -sINITIAL_MEMORY=768MB \
               -DMEM_SIZE=0x20000000 \
               -sEXPORTED_RUNTIME_METHODS='["callMain","FS"]' \
               --embed-file build/minimal.dtb@/minimal.dtb \
               --pre-js $(WEB_JS_RESOURCES)/system-pre.js
else
CFLAGS_emcc += -sINITIAL_MEMORY=320MB \
               -DMEM_SIZE=0x10000000 \
               -sEXPORTED_RUNTIME_METHODS='["callMain","FS"]' \
               --embed-file build/jit-bf.elf@/jit-bf.elf \
               --embed-file build/coro.elf@/coro.elf \
               --embed-file build/fibonacci.elf@/fibonacci.elf \
               --embed-file build/hello.elf@/hello.elf \
               --embed-file build/ieee754.elf@/ieee754.elf \
               --embed-file build/perfcount.elf@/perfcount.elf \
               --embed-file build/readelf.elf@/readelf.elf \
               --embed-file build/smolnes.elf@/smolnes.elf \
               --embed-file build/riscv32@/riscv32 \
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

# Stage Quake's pak0.pak (lives under $(OUT)/id1/) to a flat name next to the
# WASM bundle so the dev server and deploy treat it like any other WEB_FILE.
# user.html fetches it and writes it back to /id1/pak0.pak in MEMFS.
$(OUT)/pak0.pak: $(QUAKE_DATA)
	$(Q)cp $(OUT)/id1/pak0.pak $@

# Build the web-deployed rootfs by splicing /etc/init.d/S99automount into the
# upstream rootfs.cpio. Splicing preserves character device nodes (/dev/console
# etc.) which a non-root extract+repack would silently drop, leaving the guest
# init without a stdio console. See tools/cpio-inject.py for the format.
$(OUT)/linux-image/rootfs.web.cpio: $(OUT)/linux-image/rootfs.cpio \
                                    tools/rootfs-automount.sh \
                                    tools/cpio-inject.py
	$(VECHO) "  CPIO\t$@\n"
	$(Q)python3 tools/cpio-inject.py \
	    $(OUT)/linux-image/rootfs.cpio \
	    $@ \
	    etc/init.d/S99automount \
	    tools/rootfs-automount.sh
	$(Q)cpio -t < $@ 2>/dev/null | grep -q '^etc/init.d/S99automount$$' || \
	    { echo "ERROR: S99automount missing from $@"; exit 1; }
	$(Q)cpio -t < $@ 2>/dev/null | grep -q '^dev/console$$' || \
	    { echo "ERROR: dev/console missing from $@ (device nodes dropped)"; exit 1; }
	$(VECHO) "  OK\trootfs.web.cpio: S99automount injected, /dev preserved\n"

# Bundle the timidity instrument set as tar plus tar.gz so browsers with
# DecompressionStream get the smaller download while older Safari/Firefox can
# still fetch a plain tar and use the same JS untar path.
# COPYFILE_DISABLE keeps macOS BSD tar from emitting AppleDouble (._*) files;
# --no-xattrs (where supported) drops any extended attributes the JS untar
# would otherwise have to filter out. Both are silently ignored on Linux.
$(OUT)/timidity.tar: $(TIMIDITY_DATA)
	$(VECHO) "  TAR\t$@\n"
	$(Q)COPYFILE_DISABLE=1 tar --no-xattrs -cf $@ -C $(OUT)/timidity . 2>/dev/null || \
	    COPYFILE_DISABLE=1 tar -cf $@ -C $(OUT)/timidity .

$(OUT)/timidity.tar.gz: $(OUT)/timidity.tar
	$(VECHO) "  TARGZ\t$@\n"
	$(Q)gzip -9 -c $< > $@

# xterm.js terminal library for web UI.
# Fetched at build time from jsdelivr (primary) with unpkg as a backup. Both
# serve the same npm payload; jsdelivr is friendlier to CI runners while
# unpkg has been observed to return 403 to GitHub Actions traffic.
# Package renamed from 'xterm' to '@xterm/xterm' in v6.0.0.
XTERM_VERSION := 6.0.0
XTERM_VENDOR := $(ASSETS)/vendor
XTERM_JS := $(XTERM_VENDOR)/xterm.min.js
XTERM_CSS := $(XTERM_VENDOR)/xterm.min.css

XTERM_JS_URLS := \
    https://cdn.jsdelivr.net/npm/@xterm/xterm@$(XTERM_VERSION)/lib/xterm.js \
    https://unpkg.com/@xterm/xterm@$(XTERM_VERSION)/lib/xterm.js
XTERM_CSS_URLS := \
    https://cdn.jsdelivr.net/npm/@xterm/xterm@$(XTERM_VERSION)/css/xterm.css \
    https://unpkg.com/@xterm/xterm@$(XTERM_VERSION)/css/xterm.css

$(XTERM_VENDOR):
	$(Q)mkdir -p $@

# Fetch with --fail so a 4xx/5xx is a build error instead of writing an HTML
# error page over the bundle. Falls through the URL list until one succeeds.
$(XTERM_JS): | $(XTERM_VENDOR)
	$(VECHO) "  FETCH\t$@\n"
	$(Q)set -e; ok=0; \
	    for u in $(XTERM_JS_URLS); do \
	        if curl -fsSL --retry 3 -o $@ "$$u"; then ok=1; break; fi; \
	    done; \
	    [ $$ok -eq 1 ] || { echo "ERROR: failed to fetch $@"; exit 1; }

$(XTERM_CSS): | $(XTERM_VENDOR)
	$(VECHO) "  FETCH\t$@\n"
	$(Q)set -e; ok=0; \
	    for u in $(XTERM_CSS_URLS); do \
	        if curl -fsSL --retry 3 -o $@ "$$u"; then ok=1; break; fi; \
	    done; \
	    [ $$ok -eq 1 ] || { echo "ERROR: failed to fetch $@"; exit 1; }

XTERM_DATA := $(XTERM_JS) $(XTERM_CSS)

# Dependencies for WASM build
# System mode: kernel image only (no audio/games)
# User mode: ELFs, games, and timidity for MIDI audio
ifeq ($(CONFIG_SYSTEM),y)
deps_emcc += artifact
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

# Emscripten 3.x emits $(BIN).worker.js for pthread builds (SDL/system mode on
# the documented 3.1.51 toolchain); Emscripten 4.x inlines the worker code
# into the main JS file. Copy the sidecar only when it exists so the build is
# correct on both toolchains and a pthread build doesn't ship missing it.
define cp-web-worker
    $(Q)if [ -f $(BIN).worker.js ]; then cp $(BIN).worker.js $(DEMO_DIR)/; fi
endef

STATIC_WEB_FILES := $(WEB_JS_RESOURCES)/coi-serviceworker.min.js \
                    $(XTERM_JS) $(XTERM_CSS)
ifeq ($(CONFIG_SYSTEM),y)
STATIC_WEB_FILES += $(WEB_HTML_RESOURCES)/system.html
else
STATIC_WEB_FILES += $(WEB_HTML_RESOURCES)/user.html
endif

# Landing page is installed once at the unified demo root so the
# "User Mode" / "System Mode" cards resolve to ./user/ and ./system/.
LANDING_PAGE := $(WEB_HTML_RESOURCES)/demo-index.html

start_web_deps := check-demo-dir-exist $(BIN) $(XTERM_DATA)
ifeq ($(CONFIG_SYSTEM),y)
# rootfs.web.cpio is the upstream rootfs.cpio with an /etc/init.d/S99automount
# overlay so the guest auto-mounts /dev/vda at /mnt during boot.
start_web_deps += $(BUILD_DTB) $(BUILD_DTB2C) $(OUT)/linux-image/rootfs.web.cpio
else
# User mode also stages large game data alongside the WASM bundle so the
# WEB_FILES copy step succeeds. These targets pull from DOOM_DATA/QUAKE_DATA
# /TIMIDITY_DATA which deps_emcc already triggers; the rules above just
# re-stage them under flat names suitable for the dev server.
start_web_deps += $(OUT)/DOOM1.WAD $(OUT)/pak0.pak \
                  $(OUT)/timidity.tar $(OUT)/timidity.tar.gz
endif

# Populate the demo tree for the configured mode without starting a server.
# Useful for building both modes (run twice with/without ENABLE_SYSTEM=1) and
# then serving them together via `make serve-web`.
prepare-web: $(start_web_deps)
	$(Q)rm -f $(DEMO_DIR)/*.html
	$(foreach T, $(WEB_FILES), $(call cp-web-file, $(T)))
	$(foreach T, $(STATIC_WEB_FILES), $(call cp-web-file, $(T)))
	$(call cp-web-worker)
ifeq ($(CONFIG_SYSTEM),y)
	$(Q)cp build/linux-image/Image $(DEMO_DIR)/
	$(Q)cp $(OUT)/linux-image/rootfs.web.cpio $(DEMO_DIR)/rootfs.cpio
endif
	$(Q)mv $(DEMO_DIR)/*.html $(DEMO_DIR)/index.html
	$(Q)cp $(LANDING_PAGE) $(DEMO_DIR_BASE)/index.html

# Serve whatever is currently under $(DEMO_DIR_BASE). Run `prepare-web` first
# for one or both modes.
serve-web:
	$(Q)python3 tools/dev-server.py --bind $(DEMO_IP) --port $(DEMO_PORT) --directory $(DEMO_DIR_BASE)

start-web: prepare-web serve-web

# Compressed web build for production deployment
# Creates gzip and brotli compressed versions of large assets
compress-web: $(start_web_deps)
	$(Q)rm -f $(DEMO_DIR)/*.html $(DEMO_DIR)/*.gz $(DEMO_DIR)/*.br
	$(foreach T, $(WEB_FILES), $(call cp-web-file, $(T)))
	$(foreach T, $(STATIC_WEB_FILES), $(call cp-web-file, $(T)))
	$(call cp-web-worker)
ifeq ($(CONFIG_SYSTEM),y)
	$(Q)cp build/linux-image/Image $(DEMO_DIR)/
	$(Q)cp $(OUT)/linux-image/rootfs.web.cpio $(DEMO_DIR)/rootfs.cpio
endif
	$(Q)mv $(DEMO_DIR)/*.html $(DEMO_DIR)/index.html
	$(Q)cp $(LANDING_PAGE) $(DEMO_DIR_BASE)/index.html
	@$(call notice, Compressing web assets...)
	$(Q)gzip -9 -k $(DEMO_DIR)/rv32emu.wasm 2>/dev/null || true
	$(Q)gzip -9 -k $(DEMO_DIR)/rv32emu.js 2>/dev/null || true
	$(Q)if command -v brotli >/dev/null 2>&1; then \
		brotli -9 -k $(DEMO_DIR)/rv32emu.wasm 2>/dev/null || true; \
		brotli -9 -k $(DEMO_DIR)/rv32emu.js 2>/dev/null || true; \
	fi
	@$(call notice, Compression complete)
	$(Q)ls -lh $(DEMO_DIR)/rv32emu.* | awk '{print $$5, $$9}'

.PHONY: check-demo-dir-exist prepare-web serve-web start-web compress-web

endif # CC_IS_EMCC

endif # _MK_WASM_INCLUDED
