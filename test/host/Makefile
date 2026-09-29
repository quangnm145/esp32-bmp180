# Host unit tests (gcc, no ESP-IDF). Usage: make -C test/host   (or: make -C test/host clean test)
LIB     := ../../lib
CC      ?= gcc
CFLAGS  := -std=c11 -O1 -g -Wall -Wextra -Werror -I stubs -I . \
           -I $(LIB)/signal_filter -I $(LIB)/data_pool
LDLIBS  := -lm
BUILD   := build
TESTS   := $(BUILD)/test_signal_filter $(BUILD)/test_data_pool

.PHONY: all test clean
all: test

$(BUILD) out:
	mkdir -p $@

$(BUILD)/test_signal_filter: test_signal_filter.c $(LIB)/signal_filter/signal_filter.c test_common.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_signal_filter.c $(LIB)/signal_filter/signal_filter.c $(LDLIBS)

# data_pool.c is #included by the test (to reset its static state between cases).
$(BUILD)/test_data_pool: test_data_pool.c $(LIB)/data_pool/data_pool.c test_common.h $(wildcard stubs/*.h stubs/freertos/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_data_pool.c $(LDLIBS)

test: $(TESTS) | out
	@rc=0; for t in $(TESTS); do echo "== $$t"; ./$$t || rc=1; done; exit $$rc

clean:
	rm -rf $(BUILD) out
