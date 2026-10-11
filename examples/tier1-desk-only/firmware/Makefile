# Tier 1 firmware core - host build and tests.
#   make test     build and run all tests, then regenerate test vectors
#   make vectors  build gen_vectors and write test/payload_vectors.json
#   make clean

CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Werror -pedantic -O2 -g -Iinclude
BUILD   = build

CORE_SRC = src/payload.c src/queue.c src/rlog.c src/nvstate.c src/rtsm.c
TEST_SRC = test/test_main.c test/test_payload.c test/test_rtsm.c test/sim_hal.c
HDRS     = $(wildcard include/*.h) test/test.h test/sim_hal.h

.PHONY: all test vectors clean

all: $(BUILD)/test_runner $(BUILD)/gen_vectors

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_runner: $(CORE_SRC) $(TEST_SRC) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -Itest -o $@ $(CORE_SRC) $(TEST_SRC)

$(BUILD)/gen_vectors: test/gen_vectors.c src/payload.c include/payload.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ test/gen_vectors.c src/payload.c

test: $(BUILD)/test_runner vectors
	./$(BUILD)/test_runner

vectors: $(BUILD)/gen_vectors
	./$(BUILD)/gen_vectors test/payload_vectors.json

clean:
	rm -rf $(BUILD)
