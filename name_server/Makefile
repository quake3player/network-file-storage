CC ?= gcc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -I../common/include -Iinclude
LDFLAGS ?=
COMMON_LIB := ../common/libprotocol.a

SRCS := src/main.c src/file_index.c
OBJS := $(SRCS:.c=.o)
TARGET := name_server

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) $(COMMON_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(COMMON_LIB):
	$(MAKE) -C ../common libprotocol.a

clean:
	rm -f $(TARGET) $(OBJS)
	$(MAKE) -C ../common clean
