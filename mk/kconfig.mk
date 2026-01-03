# Kconfig Integration
#
# Provides configuration menu and .config management using kconfiglib.

ifndef _MK_KCONFIG_INCLUDED
_MK_KCONFIG_INCLUDED := 1

KCONFIG_DIR := tools/kconfig
KCONFIG := configs/Kconfig
CONFIG_HEADER := src/rv32emu_config.h
KCONFIGLIB_REPO := https://github.com/sysprog21/Kconfiglib

# Download Kconfig tools if missing
# Handles partial clones by removing incomplete directory before re-cloning
# Safety: validates KCONFIG_DIR is non-empty and under project root before rm -rf
$(KCONFIG_DIR)/kconfiglib.py:
	$(VECHO) "Downloading Kconfig tools...\n"
	$(Q)if [ -z "$(KCONFIG_DIR)" ]; then \
		echo "Error: KCONFIG_DIR is empty"; exit 1; \
	fi
	$(Q)case "$(KCONFIG_DIR)" in \
		/*|..*) echo "Error: KCONFIG_DIR must be relative path under project"; exit 1 ;; \
	esac
	$(Q)if [ -d "$(KCONFIG_DIR)" ] && [ ! -f "$(KCONFIG_DIR)/kconfiglib.py" ]; then \
		echo "Removing incomplete Kconfig directory..."; \
		rm -rf "$(KCONFIG_DIR)"; \
	fi
	$(Q)git clone --depth=1 -q $(KCONFIGLIB_REPO) $(KCONFIG_DIR)
	@echo "Kconfig tools installed to $(KCONFIG_DIR)"

# Ensure all Kconfig tools exist
$(KCONFIG_DIR)/menuconfig.py $(KCONFIG_DIR)/defconfig.py $(KCONFIG_DIR)/genconfig.py \
$(KCONFIG_DIR)/oldconfig.py $(KCONFIG_DIR)/savedefconfig.py: $(KCONFIG_DIR)/kconfiglib.py

# Run environment detection before showing menu
env-check:
	@echo "Checking build environment..."
	@python3 tools/detect-env.py --summary
	@echo ""
	@if python3 tools/detect-env.py --have-emcc 2>/dev/null | grep -q y; then \
		echo "[WebAssembly] Emscripten detected. 'WebAssembly' build target available in 'make config'."; \
	else \
		echo "[WebAssembly] Emscripten not found. Install emsdk to enable WASM builds."; \
	fi
	@echo ""

# Interactive configuration
config: env-check $(KCONFIG_DIR)/menuconfig.py
	@python3 $(KCONFIG_DIR)/menuconfig.py $(KCONFIG)
	@python3 $(KCONFIG_DIR)/genconfig.py --header-path $(CONFIG_HEADER) $(KCONFIG)
	@echo "Configuration saved to .config and $(CONFIG_HEADER)"

# Apply default configuration (supports CONFIG=name for named configs)
defconfig: $(KCONFIG_DIR)/defconfig.py
	@if [ -n "$(CONFIG)" ]; then \
		if [ -f "configs/$(CONFIG)_defconfig" ]; then \
			echo "Applying configs/$(CONFIG)_defconfig..."; \
			python3 $(KCONFIG_DIR)/defconfig.py --kconfig $(KCONFIG) configs/$(CONFIG)_defconfig; \
		else \
			echo "Error: configs/$(CONFIG)_defconfig not found"; exit 1; \
		fi; \
	else \
		echo "Applying default configuration..."; \
		python3 $(KCONFIG_DIR)/defconfig.py --kconfig $(KCONFIG) configs/defconfig; \
	fi
	@python3 $(KCONFIG_DIR)/genconfig.py --header-path $(CONFIG_HEADER) $(KCONFIG)
	@echo "Configuration applied."

# Pattern rule for named defconfigs (e.g., make jit_defconfig)
%_defconfig: $(KCONFIG_DIR)/defconfig.py
	@if [ -f "configs/$*_defconfig" ]; then \
		echo "Applying configs/$*_defconfig..."; \
		python3 $(KCONFIG_DIR)/defconfig.py --kconfig $(KCONFIG) configs/$*_defconfig; \
		python3 $(KCONFIG_DIR)/genconfig.py --header-path $(CONFIG_HEADER) $(KCONFIG); \
		echo "Configuration applied."; \
	else \
		echo "Error: configs/$*_defconfig not found"; exit 1; \
	fi

# Update configuration after Kconfig changes
oldconfig: $(KCONFIG_DIR)/oldconfig.py
	@python3 $(KCONFIG_DIR)/oldconfig.py $(KCONFIG)
	@python3 $(KCONFIG_DIR)/genconfig.py --header-path $(CONFIG_HEADER) $(KCONFIG)

# Save current configuration as minimal defconfig
savedefconfig: $(KCONFIG_DIR)/savedefconfig.py
	@python3 $(KCONFIG_DIR)/savedefconfig.py --kconfig $(KCONFIG) --out defconfig.new
	@echo "Saved minimal config to defconfig.new"

# Explicit target to download/update Kconfig tools
kconfig-tools: $(KCONFIG_DIR)/kconfiglib.py
	@echo "Kconfig tools are ready in $(KCONFIG_DIR)"

# Generate .config if missing (order-only to preserve user customizations)
.config: | $(KCONFIG_DIR)/defconfig.py
	@python3 $(KCONFIG_DIR)/defconfig.py --kconfig $(KCONFIG) configs/defconfig
	@echo "Generated .config from default configuration"

# Auto-generate config header from .config
$(CONFIG_HEADER): .config $(KCONFIG_DIR)/genconfig.py
	@python3 $(KCONFIG_DIR)/genconfig.py --header-path $(CONFIG_HEADER) $(KCONFIG)
	@echo "Generated $(CONFIG_HEADER)"

.PHONY: config defconfig oldconfig savedefconfig env-check kconfig-tools

endif # _MK_KCONFIG_INCLUDED
