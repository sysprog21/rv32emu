CFLAGS_emcc ?=
deps_emcc :=
ASSETS := assets/wasm
WEB_HTML_RESOURCES := $(ASSETS)/html
WEB_JS_RESOURCES := $(ASSETS)/js
EXPORTED_FUNCS := _main,_indirect_rv_halt
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
	       --embed-file build@/ \
	       --embed-file build/riscv32@/riscv32 \
	       --embed-file build/timidity@/etc/timidity \
	       -DMEM_SIZE=0x40000000 \
	       -DCYCLE_PER_STEP=2000000 \
	       --pre-js $(WEB_JS_RESOURCES)/pre.js \
	       -O3 \
	       -w

$(OUT)/elf_list.js: tools/gen-elf-list-js.py
	$(Q)tools/gen-elf-list-js.py > $@

# used to download all dependencies of elf executable and bundle into single wasm
deps_emcc += artifact $(OUT)/elf_list.js $(DOOM_DATA) $(QUAKE_DATA) $(TIMIDITY_DATA)

# check browser MAJOR version if supports TCO
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

# FIXME: for Windows
ifeq ($(UNAME_S),Darwin)
    CHROME_MAJOR_VERSION_CHECK_CMD := "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --version | awk '{print $$3}' | cut -f1 -d.
    FIREFOX_MAJOR_VERSION_CHECK_CMD := /Applications/Firefox.app/Contents/MacOS/firefox --version | awk '{print $$3}' | cut -f1 -d.
else ifeq ($(UNAME_S),Linux)
    CHROME_MAJOR_VERSION_CHECK_CMD := google-chrome --version | awk '{print $$3}' | cut -f1 -d.
    FIREFOX_MAJOR_VERSION_CHECK_CMD := firefox -v | awk '{print $$3}' | cut -f1 -d.
endif
CHROME_MAJOR := $(shell $(CHROME_MAJOR_VERSION_CHECK_CMD))
FIREFOX_MAJOR := $(shell $(FIREFOX_MAJOR_VERSION_CHECK_CMD))

# Chrome
ifeq ($(shell echo $(CHROME_MAJOR)\>=$(CHROME_SUPPORT_TCO_AT_MAJOR) | bc), 1)
    $(info $(shell echo "$(GREEN)$(CHROME_SUPPORT_TCO_INFO)$(NC)"))
else
    $(warning $(shell echo "$(YELLOW)$(CHROME_NO_SUPPORT_TCO_WARNING)$(NC)"))
endif

# Firefox
ifeq ($(shell echo $(FIREFOX_MAJOR)\>=$(FIREFOX_SUPPORT_TCO_AT_MAJOR) | bc), 1)
    $(info $(shell echo "$(GREEN)$(FIREFOX_SUPPORT_TCO_INFO)$(NC)"))
else
    $(warning $(shell echo "$(YELLOW)$(FIREFOX_NO_SUPPORT_TCO_WARNING)$(NC)"))
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
STATIC_WEB_FILES := $(WEB_HTML_RESOURCES)/index.html \
		    $(WEB_JS_RESOURCES)/coi-serviceworker.min.js

start-web: check-demo-dir-exist $(BIN)
	$(foreach T, $(WEB_FILES), $(call cp-web-file, $(T)))
	$(foreach T, $(STATIC_WEB_FILES), $(call cp-web-file, $(T)))
	$(Q)python3 -m http.server --bind $(DEMO_IP) $(DEMO_PORT) --directory $(DEMO_DIR)
endif
