CC ?= gcc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -Iinclude
AR ?= ar

LIB_OBJS := src/protocol.o src/persistence.o src/net.o
TEST_OBJS := tests/test_protocol.o

LIB_TARGET := libprotocol.a
TEST_TARGET := tests/test_protocol

.PHONY: all clean test

all: $(LIB_TARGET)

$(LIB_TARGET): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(TEST_TARGET): $(LIB_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(LIB_TARGET) $(LIB_OBJS) $(TEST_OBJS) $(TEST_TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)
