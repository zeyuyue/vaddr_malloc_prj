CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -I./include
LDFLAGS := -pthread

SRC_DIR   := src
TEST_DIR  := tests
BUILD_DIR := build

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
LIB  := $(BUILD_DIR)/libvaddr.a

TEST_SRCS := $(wildcard $(TEST_DIR)/ttos*.c)
TESTS     := $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/%, $(TEST_SRCS))

.PHONY: all test clean asan

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	ar rcs $@ $^

$(BUILD_DIR)/%: $(TEST_DIR)/%.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lvaddr $(LDFLAGS) -o $@

test: $(TESTS)
	@failed=0; \
	for t in $(TESTS); do \
		echo "=== $$t ==="; \
		$$t || failed=1; \
	done; \
	exit $$failed

# Build and run tests with AddressSanitizer
asan: CFLAGS += -fsanitize=address -g
asan: LDFLAGS += -fsanitize=address
asan: test

clean:
	rm -rf $(BUILD_DIR)
