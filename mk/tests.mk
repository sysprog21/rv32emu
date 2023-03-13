CACHE_TEST_DIR := tests/cache
CACHE_BUILD_DIR := build/cache
TARGET := test-cache

CACHE_OBJS := \
	test-cache.o

CACHE_OBJS := $(addprefix $(CACHE_BUILD_DIR)/, $(CACHE_OBJS))
OBJS += $(CACHE_OBJS)
deps += $(CACHE_OBJS:%.o=%.o.d)


CACHE_CHECK_ELF_FILES := \
	cache-new \
	cache-put \
	cache-get \
	cache-lru-replace \
	cache-lfu-replace \
	cache-lfu-ghost-replace 

CACHE_OUT = $(addprefix $(CACHE_BUILD_DIR)/, $(CACHE_CHECK_ELF_FILES:%=%.out))

tests : $(CACHE_OUT)
	$(Q)$(foreach e,$(CACHE_CHECK_ELF_FILES),\
		$(PRINTF) "Running $(e) ... "; \
		if cmp $(CACHE_TEST_DIR)/$(e).expect $(CACHE_BUILD_DIR)/$(e).out; then \
		$(call notice, [OK]); \
		else \
		$(PRINTF) "Failed.\n"; \
		exit 1; \
		fi; \
	)

$(CACHE_OUT): $(TARGET)
	$(Q)$(foreach e,$(CACHE_CHECK_ELF_FILES),\
		$(CACHE_BUILD_DIR)/$(TARGET) $(CACHE_TEST_DIR)/$(e).in > $(CACHE_BUILD_DIR)/$(e).out; \
	)

$(TARGET): $(CACHE_OBJS)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) $^ build/cache.o -o $(CACHE_BUILD_DIR)/$(TARGET)
	
$(CACHE_BUILD_DIR)/%.o: $(CACHE_TEST_DIR)/%.c 
	$(VECHO) "  CC\t$@\n"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) -o $@ $(CFLAGS) -I./src -c -MMD -MF $@.d $<