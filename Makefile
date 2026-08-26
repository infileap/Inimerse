CC = gcc
CFLAGS = -D_GNU_SOURCE -D_stricmp=strcasecmp -Wall -Wextra -std=c11 -I src -I src/parser -I src/compiler -I src/vm -I src/runtime -I src/mod -I src/common -I src/lexer
LDFLAGS = -lm

SRCS = src/main.c \
       src/platform/platform.c src/platform/dir.c src/platform/thread.c src/platform/fiber.c \
       src/common/common.c \
       src/lexer/lexer.c \
       src/parser/parser.c \
       src/compiler/bytecode.c \
       src/compiler/compiler.c \
       src/vm/vm.c \
       

ifeq ($(OS),Windows_NT)
SRCS += src/runtime/runtime.c
SRCS += src/mod/mod.c
else
SRCS += src/runtime/runtime_posix.c
SRCS += src/mod/mod_posix.c
SRCS += src/platform/posix_stubs.c
endif

OBJS = $(SRCS:.c=.o)
TARGET = inimerse

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean linux check wasm

linux: clean all

check:
	 if command -v node >/dev/null 2>&1; then node tools/regression.js; else echo "node not installed; skipping protocol regression"; fi
	 $(MAKE) linux
	 ./inimerse --version
	 ./inimerse where

wasm:
	 @if command -v emcc >/dev/null 2>&1; then echo "WASM toolchain detected: $$(emcc --version | head -1)"; echo "WASM target wiring pending host import table"; \
	 elif command -v clang >/dev/null 2>&1 && clang --target=wasm32-wasi --version >/dev/null 2>&1; then echo "WASI clang detected"; echo "WASM target wiring pending host import table"; \
	 else echo "WASM toolchain not found. Install emscripten or wasi-sdk."; exit 2; fi

platform-probe:
	$(CC) $(CFLAGS) -o platform_probe src/platform/platform.c src/platform/thread.c src/platform/fiber.c src/platform/platform_probe.c

fiber-probe:
	$(CC) $(CFLAGS) -o fiber_probe src/platform/fiber.c src/platform/fiber_probe.c

process-probe:
	$(CC) $(CFLAGS) -o process_probe src/platform/process.c src/platform/process_probe.c

socket-probe:
	$(CC) $(CFLAGS) -o socket_probe src/platform/socket.c src/platform/socket_probe.c

.PHONY: platform-probe fiber-probe process-probe socket-probe
