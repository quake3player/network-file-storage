CC ?= gcc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -I../common/include -Iinclude -pthread
LDFLAGS ?= -pthread
COMMON_LIB := ../common/libprotocol.a

SRCS := src/main.c src/storage_engine.c
OBJS := $(SRCS:.c=.o)
TARGET := storage_server

TEST_SRCS := tests/test_engine.c
TEST_OBJS := $(TEST_SRCS:.c=.o)
TEST_TARGET := tests/test_engine

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS) $(COMMON_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(COMMON_LIB):
	$(MAKE) -C ../common libprotocol.a

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -I../common/include -Iinclude -c $< -o $@

$(TEST_TARGET): $(TEST_OBJS) src/storage_engine.o $(COMMON_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS) $(TEST_TARGET) $(TEST_OBJS)
	$(MAKE) -C ../common clean

test: $(TEST_TARGET)
	./$(TEST_TARGET)
