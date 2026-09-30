bytecode_capture_probe: src/compiler/bytecode_capture_probe.c src/compiler/bytecode.c src/compiler/bytecode.h src/common/common.h src/platform/platform.o src/compilation/deps.c src/compilation/deps.h
	$(CC) $(CFLAGS) -Isrc -Isrc/common -Isrc/platform -o $@ $< src/compiler/bytecode.c src/compilation/deps.c src/platform/platform.o -lm
