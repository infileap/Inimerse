CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I src -I src/parser -I src/compiler -I src/vm -I src/runtime -I src/mod -I src/common -I src/lexer
LDFLAGS = -lm

SRCS = src/main.c \
       src/platform/platform.c src/platform/dir.c \
       src/common/common.c \
       src/lexer/lexer.c \
       src/parser/parser.c \
       src/compiler/bytecode.c \
       src/compiler/compiler.c \
       src/vm/vm.c \
       src/runtime/runtime.c \
       src/mod/mod.c

OBJS = $(SRCS:.c=.o)
TARGET = inimerse

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

platform-probe:
	$(CC) $(CFLAGS) -o platform_probe src/platform/platform.c src/platform/thread.c src/platform/fiber.c src/platform/platform_probe.c

fiber-probe:
	$(CC) $(CFLAGS) -o fiber_probe src/platform/fiber.c src/platform/fiber_probe.c

process-probe:
	$(CC) $(CFLAGS) -o process_probe src/platform/process.c src/platform/process_probe.c

socket-probe:
	$(CC) $(CFLAGS) -o socket_probe src/platform/socket.c src/platform/socket_probe.c

.PHONY: platform-probe fiber-probe process-probe socket-probe
