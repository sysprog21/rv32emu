CACHE_TEST_SRCDIR := tests/cache
CACHE_TEST_OUTDIR := build/cache
CACHE_TEST_TARGET := $(CACHE_TEST_OUTDIR)/test-cache

MAP_TEST_SRCDIR := tests/map
MAP_TEST_OUTDIR:= build/map
MAP_TEST_TARGET := $(MAP_TEST_OUTDIR)/test-map

CACHE_TEST_OBJS := \
	test-cache.o

MAP_TEST_OBJS := \
	test-map.o \
	mt19937.o

CACHE_TEST_OBJS := $(addprefix $(CACHE_TEST_OUTDIR)/, $(CACHE_TEST_OBJS)) \
		   $(OUT)/cache.o $(OUT)/mpool.o
OBJS += $(CACHE_TEST_OBJS)
deps += $(CACHE_TEST_OBJS:%.o=%.o.d)

MAP_TEST_OBJS := $(addprefix $(MAP_TEST_OUTDIR)/, $(MAP_TEST_OBJS)) \
		 $(OUT)/map.o
OBJS += $(MAP_TEST_OBJS)
deps += $(MAP_TEST_OBJS:%.o=%.o.d)

# Check adaptive replacement cache policy is enabled or not, default is LFU
ifeq ($(ENABLE_ARC), 1)
CACHE_TEST_ACTIONS := \
	arc/cache-new \
	arc/cache-put \
	arc/cache-get \
	arc/cache-lru-replace \
	arc/cache-lfu-replace \
	arc/cache-lru-ghost-replace \
	arc/cache-lfu-ghost-replace
else
CACHE_TEST_ACTIONS := \
	lfu/cache-new \
	lfu/cache-put \
	lfu/cache-get \
	lfu/cache-lfu-replace
endif

CACHE_TEST_OUT = $(addprefix $(CACHE_TEST_OUTDIR)/, $(CACHE_TEST_ACTIONS:%=%.out))
MAP_TEST_OUT = $(MAP_TEST_TARGET).out

tests : run-test-cache run-test-map

run-test-cache: $(CACHE_TEST_OUT)
	$(Q)$(foreach e,$(CACHE_TEST_ACTIONS),\
	    $(PRINTF) "Running $(e) ... "; \
	    if cmp $(CACHE_TEST_SRCDIR)/$(e).expect $(CACHE_TEST_OUTDIR)/$(e).out; then \
	    $(call notice, [OK]); \
	    else \
	    $(PRINTF) "Failed.\n"; \
	    exit 1; \
	    fi; \
	)

run-test-map: $(MAP_TEST_OUT)
	$(Q)$(MAP_TEST_TARGET)
	$(VECHO) "Running test-map ... "; \
	if [ $$? -eq 0 ]; then \
	$(call notice, [OK]); \
	else \
	$(PRINTF) "Failed.\n"; \
	fi;

$(CACHE_TEST_OUT): $(CACHE_TEST_TARGET)
	$(Q)$(foreach e,$(CACHE_TEST_ACTIONS),\
	    $(CACHE_TEST_TARGET) $(CACHE_TEST_SRCDIR)/$(e).in > $(CACHE_TEST_OUTDIR)/$(e).out; \
	)

$(CACHE_TEST_TARGET): $(CACHE_TEST_OBJS)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) $^ -o $@

$(CACHE_TEST_OUTDIR)/%.o: $(CACHE_TEST_SRCDIR)/%.c
	$(VECHO) "  CC\t$@\n"
	$(Q)mkdir -p $(dir $@)/arc $(dir $@)/lfu
	$(Q)$(CC) -o $@ $(CFLAGS) -I./src -c -MMD -MF $@.d $<

$(MAP_TEST_OUT): $(MAP_TEST_TARGET)
	$(Q)touch $@

$(MAP_TEST_TARGET): $(MAP_TEST_OBJS)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) $^ -o $@

$(MAP_TEST_OUTDIR)/%.o: $(MAP_TEST_SRCDIR)/%.c
	$(VECHO) "  CC\t$@\n"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) -o $@ $(CFLAGS) -I./src -c -MMD -MF $@.d $<
