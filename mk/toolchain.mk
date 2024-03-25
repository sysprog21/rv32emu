CC_IS_EMCC :=
CC_IS_CLANG :=
CC_IS_GCC :=
ifneq ($(shell $(CC) --version | head -n 1 | grep emcc),)
    CC_IS_EMCC := 1

    EMCC_VERSION := $(shell $(CC) --version | head -n 1 | cut -f10 -d ' ')
    EMCC_MAJOR := $(shell echo $(EMCC_VERSION) | cut -f1 -d.)
    EMCC_MINOR := $(shell echo $(EMCC_VERSION) | cut -f2 -d.)
    EMCC_PATCH := $(shell echo $(EMCC_VERSION) | cut -f3 -d.)

    # When the emcc version is not 3.1.51, the latest SDL2_mixer library is fetched by emcc and music might not be played in the web browser
    SDL_MUSIC_PLAY_AT_EMCC_MAJOR := 3
    SDL_MUSIC_PLAY_AT_EMCC_MINOR := 1
    SDL_MUSIC_PLAY_AT_EMCC_PATCH := 51
    SDL_MUSIC_CANNOT_PLAY_WARNING := Video games music might not be played. You may switch emcc to version $(SDL_MUSIC_PLAY_AT_EMCC_MAJOR).$(SDL_MUSIC_PLAY_AT_EMCC_MINOR).$(SDL_MUSIC_PLAY_AT_EMCC_PATCH)
    ifeq ($(shell echo $(EMCC_MAJOR)\==$(SDL_MUSIC_PLAY_AT_EMCC_MAJOR) | bc), 1)
        ifeq ($(shell echo $(EMCC_MINOR)\==$(SDL_MUSIC_PLAY_AT_EMCC_MINOR) | bc), 1)
            ifeq ($(shell echo $(EMCC_PATCH)\==$(SDL_MUSIC_PLAY_AT_EMCC_PATCH) | bc), 1)
	        # do nothing
            else
                $(warning $(SDL_MUSIC_CANNOT_PLAY_WARNING))
            endif
        else
	    $(warning $(SDL_MUSIC_CANNOT_PLAY_WARNING))
        endif
    else
	$(warning $(SDL_MUSIC_CANNOT_PLAY_WARNING))
    endif

    # see commit 165c1a3 of emscripten
    MIMALLOC_SUPPORT_SINCE_MAJOR := 3
    MIMALLOC_SUPPORT_SINCE_MINOR := 1
    MIMALLOC_SUPPORT_SINCE_PATCH := 50
    MIMALLOC_UNSUPPORTED_WARNING := mimalloc is supported after version $(MIMALLOC_SUPPORT_SINCE_MAJOR).$(MIMALLOC_SUPPORT_SINCE_MINOR).$(MIMALLOC_SUPPORT_SINCE_PATCH)
    ifeq ($(shell echo $(EMCC_MAJOR)\>=$(MIMALLOC_SUPPORT_SINCE_MAJOR) | bc), 1)
        ifeq ($(shell echo $(EMCC_MINOR)\>=$(MIMALLOC_SUPPORT_SINCE_MINOR) | bc), 1)
            ifeq ($(shell echo $(EMCC_PATCH)\>=$(MIMALLOC_SUPPORT_SINCE_PATCH) | bc), 1)
                CFLAGS_emcc += -sMALLOC=mimalloc
            else
                $(warning $(MIMALLOC_UNSUPPORTED_WARNING))
            endif
        else
            $(warning $(MIMALLOC_UNSUPPORTED_WARNING))
        endif
    else
        $(warning $(MIMALLOC_UNSUPPORTED_WARNING))
    endif
else ifneq ($(shell $(CC) --version | head -n 1 | grep clang),)
     CC_IS_CLANG := 1
else ifneq ($(shell $(CC) --version | grep "Free Software Foundation"),)
     CC_IS_GCC := 1
endif

CFLAGS_NO_CET :=
processor := $(shell uname -m)
ifeq ($(processor),$(filter $(processor),i386 x86_64))
    # GCC and Clang can generate support code for Intel's Control-flow
    # Enforcement Technology (CET) through this compiler flag:
    # -fcf-protection=[full]
    CFLAGS_NO_CET := -fcf-protection=none
endif

# As of Xcode 15, linker warnings are emitted if duplicate '-l' options are
# present. Until such linkopts can be deduped by the build system, we disable
# these warnings.
ifeq ($(UNAME_S),Darwin)
    ifeq ("$(CC_IS_CLANG)$(CC_IS_GCC)", "1")
        ifneq ($(shell ld -version_details | cut -f2 -d: | grep 15.0.0),)
            LDFLAGS += -Wl,-no_warn_duplicate_libraries
        endif
    endif
endif

# Supported GNU Toolchain for RISC-V
TOOLCHAIN_LIST := riscv-none-elf-      \
		  riscv32-unknown-elf- \
		  riscv64-unknown-elf- \
		  riscv-none-embed-

define check-cross-tools
$(shell which $(1)gcc >/dev/null &&
        which $(1)cpp >/dev/null &&
        echo | $(1)cpp -dM - | grep __riscv >/dev/null &&
        echo "$(1) ")
endef

# TODO: support clang/llvm based cross compilers
# TODO: support native RISC-V compilers
CROSS_COMPILE ?= $(word 1,$(foreach prefix,$(TOOLCHAIN_LIST),$(call check-cross-tools, $(prefix))))

export CROSS_COMPILE
