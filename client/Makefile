CC ?= gcc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -I../common/include -Iinclude
LDFLAGS ?=
COMMON_LIB := ../common/libprotocol.a

SRCS := main.c src/commands.c
OBJS := $(SRCS:.c=.o)
TARGET := client

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) $(COMMON_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(COMMON_LIB):
	$(MAKE) -C ../common libprotocol.a

clean:
	rm -f $(TARGET) $(OBJS)
	$(MAKE) -C ../common clean
