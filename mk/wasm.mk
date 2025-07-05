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

ifeq ("$(CC_IS_EMCC)", "1")
BIN := $(BIN).js

# TCO
CFLAGS += -mtail-call

# Build emscripten-port SDL
ifeq ($(call has, SDL), 1)
CFLAGS_emcc += -sUSE_SDL=2 -sSDL2_MIXER_FORMATS=wav,mid -sUSE_SDL_MIXER=2
OBJS_EXT += syscall_sdl.o
LDFLAGS += -pthread
endif

# More build flags
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

ifeq ($(call has, SYSTEM), 1)
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


$(OUT)/elf_list.js: tools/gen-elf-list-js.py
	$(Q)tools/gen-elf-list-js.py > $@

# used to download all dependencies of elf executable and bundle into single wasm
deps_emcc += artifact $(OUT)/elf_list.js $(DOOM_DATA) $(QUAKE_DATA) $(TIMIDITY_DATA)

# check browser version if supports TCO
CHROME_MAJOR :=
CHROME_MAJOR_VERSION_CHECK_CMD :=
CHROME_SUPPORT_TCO_AT_MAJOR := 112
CHROME_SUPPORT_TCO_INFO := Chrome supports TCO, you can use Chrome to request the wasm
CHROME_NO_SUPPORT_TCO_WARNING := Chrome not found or Chrome must have at least version $(CHROME_SUPPORT_TCO_AT_MAJOR) in MAJOR to support TCO in wasm

FIREFOX_MAJOR :=
FIREFOX_MAJOR_VERSION_CHECK_CMD :=
FIREFOX_SUPPORT_TCO_AT_MAJOR := 121
FIREFOX_SUPPORT_TCO_INFO := Firefox supports TCO, you can use Firefox to request the wasm
FIREFOX_NO_SUPPORT_TCO_WARNING := Firefox not found or Firefox must have at least version $(FIREFOX_SUPPORT_TCO_AT_MAJOR) in MAJOR to support TCO in wasm

# Check WebAssembly section at https://webkit.org/blog/16301/webkit-features-in-safari-18-2/
ifeq ($(UNAME_S),Darwin)
SAFARI_MAJOR :=
SAFARI_MINOR :=
SAFARI_VERSION_CHECK_CMD :=
SAFARI_SUPPORT_TCO_AT_MAJOR_MINOR := 18.2
SAFARI_SUPPORT_TCO_INFO := Safari supports TCO, you can use Safari to request the wasm
SAFARI_NO_SUPPORT_TCO_WARNING := Safari not found or Safari must have at least version $(SAFARI_SUPPORT_TCO_AT_MAJOR_MINOR) to support TCO in wasm
endif

# FIXME: for Windows
ifeq ($(UNAME_S),Darwin)
    CHROME_MAJOR_VERSION_CHECK_CMD := "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --version | awk '{print $$3}' | cut -f1 -d.
    FIREFOX_MAJOR_VERSION_CHECK_CMD := /Applications/Firefox.app/Contents/MacOS/firefox --version | awk '{print $$3}' | cut -f1 -d.
    SAFARI_VERSION_CHECK_CMD := mdls -name kMDItemVersion /Applications/Safari.app | sed 's/"//g' | awk '{print $$3}'
else ifeq ($(UNAME_S),Linux)
    CHROME_MAJOR_VERSION_CHECK_CMD := google-chrome --version | awk '{print $$3}' | cut -f1 -d.
    FIREFOX_MAJOR_VERSION_CHECK_CMD := firefox -v | awk '{print $$3}' | cut -f1 -d.
endif
CHROME_MAJOR := $(shell $(CHROME_MAJOR_VERSION_CHECK_CMD))
FIREFOX_MAJOR := $(shell $(FIREFOX_MAJOR_VERSION_CHECK_CMD))

# Chrome
ifeq ($(shell echo $(CHROME_MAJOR)\>=$(CHROME_SUPPORT_TCO_AT_MAJOR) | bc), 1)
    $(info $(call noticex, $(CHROME_SUPPORT_TCO_INFO)))
else
    $(warning $(call warnx, $(CHROME_NO_SUPPORT_TCO_WARNING)))
endif

# Firefox
ifeq ($(shell echo $(FIREFOX_MAJOR)\>=$(FIREFOX_SUPPORT_TCO_AT_MAJOR) | bc), 1)
    $(info $(call noticex, $(FIREFOX_SUPPORT_TCO_INFO)))
else
    $(warning $(call warnx, $(FIREFOX_NO_SUPPORT_TCO_WARNING)))
endif

# Safari
ifeq ($(UNAME_S),Darwin)
# ignore PATCH because the expression with PATCH(e.g., double dots x.x.x) becomes invalid number for the following bc cmd
SAFARI_VERSION := $(shell $(SAFARI_VERSION_CHECK_CMD))
SAFARI_MAJOR := $(shell echo $(SAFARI_VERSION) | cut -f1 -d.)
SAFARI_MINOR := $(shell echo $(SAFARI_VERSION) | cut -f2 -d.)
ifeq ($(shell echo "$(SAFARI_MAJOR).$(SAFARI_MINOR)>=$(SAFARI_SUPPORT_TCO_AT_MAJOR_MINOR)" | bc), 1)
    $(info $(call noticex, $(SAFARI_SUPPORT_TCO_INFO)))
else
    $(warning $(call warnx, $(SAFARI_NO_SUPPORT_TCO_WARNING)))
endif
endif

# used to serve wasm locally
DEMO_IP := 127.0.0.1
DEMO_PORT := 8000

# check if demo root directory exists and create it if not
check-demo-dir-exist:
	$(Q)if [ ! -d "$(DEMO_DIR)" ]; then \
		mkdir -p "$(DEMO_DIR)"; \
	fi

# FIXME: without $(info) generates errors
define cp-web-file
    $(Q)cp $(1) $(DEMO_DIR)
    $(info)
endef

# WEB_FILES could be cleaned and recompiled, thus do not mix these two files into WEB_FILES
STATIC_WEB_FILES := $(WEB_JS_RESOURCES)/coi-serviceworker.min.js
ifeq ($(call has, SYSTEM), 1)
STATIC_WEB_FILES += $(WEB_HTML_RESOURCES)/system.html
else
STATIC_WEB_FILES += $(WEB_HTML_RESOURCES)/user.html
endif

start_web_deps := check-demo-dir-exist $(BIN)
ifeq ($(call has, SYSTEM), 1)
start_web_deps += $(BUILD_DTB) $(BUILD_DTB2C)
endif

start-web: $(start_web_deps)
	$(Q)rm -f $(DEMO_DIR)/*.html
	$(foreach T, $(WEB_FILES), $(call cp-web-file, $(T)))
	$(foreach T, $(STATIC_WEB_FILES), $(call cp-web-file, $(T)))
	$(Q)mv $(DEMO_DIR)/*.html $(DEMO_DIR)/index.html
	$(Q)python3 -m http.server --bind $(DEMO_IP) $(DEMO_PORT) --directory $(DEMO_DIR)
endif
