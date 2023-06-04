CACHE_TEST_DIR := tests/cache
CACHE_BUILD_DIR := build/cache
CACHE_TARGET := test-cache

MAP_TEST_DIR := tests/map
MAP_BUILD_DIR:= build/map
MAP_TARGET := test-map

CACHE_OBJS := \
	test-cache.o

MAP_OBJS := \
	test-map.o

CACHE_OBJS := $(addprefix $(CACHE_BUILD_DIR)/, $(CACHE_OBJS))
OBJS += $(CACHE_OBJS)
deps += $(CACHE_OBJS:%.o=%.o.d)

MAP_OBJS := $(addprefix $(MAP_BUILD_DIR)/, $(MAP_OBJS))
OBJS += $(MAP_OBJS)
deps += $(MAP_OBJS:%.o=%.o.d)

# Check adaptive replacement cache policy is enabled or not, default is LRU
ifeq ($(ENABLE_ARC), 1)
CACHE_CHECK_ELF_FILES := \
	arc/cache-new \
	arc/cache-put \
	arc/cache-get \
	arc/cache-lru-replace \
	arc/cache-lfu-replace \
	arc/cache-lru-ghost-replace \
	arc/cache-lfu-ghost-replace
else
CACHE_CHECK_ELF_FILES := \
	lfu/cache-new \
	lfu/cache-put \
	lfu/cache-get \
	lfu/cache-lfu-replace
endif

CACHE_OUT = $(addprefix $(CACHE_BUILD_DIR)/, $(CACHE_CHECK_ELF_FILES:%=%.out))
MAP_OUT = $(addprefix $(MAP_BUILD_DIR)/$(MAP_TARGET), ".out")

tests : run_cache_test run_map_test

run_cache_test : $(CACHE_OUT)
	$(Q)$(foreach e,$(CACHE_CHECK_ELF_FILES),\
		$(PRINTF) "Running $(e) ... "; \
		if cmp $(CACHE_TEST_DIR)/$(e).expect $(CACHE_BUILD_DIR)/$(e).out; then \
		$(call notice, [OK]); \
		else \
		$(PRINTF) "Failed.\n"; \
		exit 1; \
		fi; \
	)

run_map_test: $(MAP_TARGET)

$(CACHE_OUT): $(CACHE_TARGET)
	$(Q)$(foreach e,$(CACHE_CHECK_ELF_FILES),\
		$(CACHE_BUILD_DIR)/$(CACHE_TARGET) $(CACHE_TEST_DIR)/$(e).in > $(CACHE_BUILD_DIR)/$(e).out; \
	)

$(CACHE_TARGET): $(CACHE_OBJS)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) $^ build/cache.o build/mpool.o -o $(CACHE_BUILD_DIR)/$(CACHE_TARGET)

$(CACHE_BUILD_DIR)/%.o: $(CACHE_TEST_DIR)/%.c
	$(VECHO) "  CC\t$@\n"
	$(Q)mkdir -p $(dir $@)/arc $(dir $@)/lfu
	$(Q)$(CC) -o $@ $(CFLAGS) -I./src -c -MMD -MF $@.d $<

$(MAP_TARGET): $(MAP_OBJS)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) $^ build/map.o -o $(MAP_BUILD_DIR)/$(MAP_TARGET)
	$(Q)$(MAP_BUILD_DIR)/$(MAP_TARGET)
	$(Q)$(PRINTF) "Running test-map ... "; \
	if [ $$? -eq 0 ]; then \
	$(call notice, [OK]); \
	else \
	$(PRINTF) "Failed.\n"; \
	fi;

$(MAP_BUILD_DIR)/%.o: $(MAP_TEST_DIR)/%.c
	$(VECHO) "  CC\t$@\n"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) -o $@ $(CFLAGS) -I./src -c -MMD -MF $@.d $<
