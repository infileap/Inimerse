# Inimerse Script Compilation Guide
> ⚠️ **已归档（2026-10-01）**：本文件已被 [`docs/API.md`](../API.md) / [`docs/STATUS.md`](../STATUS.md) 取代，仅作历史记录保留，结论可能已过时。
> **本次核对到的失效点**：本文件抄录的 `INIMERSE_ENGINE_SOURCES` 列表与 `src/main.c` 行号**已过时**；其探针清单中的 `websocket_probe` 并不是一个 CTest。当前列表见 `CMakeLists.txt:175-202`。


## Overview

The Inimerse engine (`inimerse`) supports two main script formats:
- **Bytecode (.inim)** - Pre-compiled bytecode for fast loading
- **Source (.im)** - Parse + compile on-demand

The engine's main function in `src/main.c` implements a unified script loader:
1. Check file extension
2. Load bytecode directly if `.inim`
3. Parse + compile from `.im` source if needed
4. Register VM globals (params) before compilation
5. Execute the compiled bytecode

## Build Process

### Required CMake Configuration
The engine sources are defined in `CMakeLists.txt` around line 147:

```cmake
set(INIMERSE_ENGINE_SOURCES
  src/main.c src/platform/platform.c src/platform/thread.c src/platform/fiber.c
  src/platform/dir.c
  src/common/common.c src/common/sha256.c src/common/ed25519.c
  src/types/typeset.c src/types/enum.c src/types/error_types.c src/types/registry.c
  src/vm/closure.c
  src/lexer/lexer.c src/parser/parser.c
  src/compiler/bytecode.c src/compiler/compiler.c
  src/vm/vm.c src/vm/params_v2.c src/vm/jit_mode.c)
```

Windows gets additional modules; POSIX gets `runtime_posix.c` and related platform modules.

### Compilation Workflow
The build command typically runs:
```bash
cmake -B build -S . -DINIMERSE_BUILD_ENGINE=ON
cmake --build build
```

This generates the `inimerse` executable (or `.exe` on Windows).

## Script Loading Flow

### Bytecode (.inim) Loading
From `src/main.c:266-273`:
```c
if (plen > 5 && strcmp(path + plen - 5, ".inim") == 0) {
    Bytecode *bc = bytecode_read_file(path);
    if (!bc) {
        if (g_err_json) {
            main_err_json("io", path, "recompile the .inim with buildc (old format)");
            return 1;
        }
        fprintf(stderr, "error: cannot load bytecode '%s' (old format? recompile with buildc)\n", path);
        return 1;
    }
    vm_load_bytecode(vm, bc);
    vm_run(vm);
    bytecode_free(bc);
    if (vm->last_error) return 1;
    return 0;
}
```

**Note**: Loading old `.inim` bytecode without modern parameters may fail; recompilation with buildc is required.

### Source (.im) Loading
From `src/main.c:284-301`:
```c
Program *prog = parse_program_file(path);
if (!prog) {
    if (g_err_json) {
        main_err_json("io", path, "check that the script path exists");
        return 1;
    }
    fprintf(stderr, "error: cannot read script '%s'\n", path);
    return 1;
}

Compiler *comp = compiler_new();

/* params first: their globals get stable indices, then main compile pre-registers them */
{
    FILE *pf = fopen(params_path, "rb");
    if (pf) { fclose(pf); vm_params_load_v2_or_legacy(vm, params_path); }
}

for (int i = 0; i < vm->globalCount; i++)
    if (vm->globals[i].name) register_global(comp, vm->globals[i].name);

compiler_compile(comp, prog);
Bytecode *bc = compiler_get_main_bytecode(comp);
vm_load_bytecode(vm, bc);
vm_run(vm);
compiler_free(comp);

if (vm->last_error) return 1;
return 0;
```

### Step-by-Step Breakdown
1. **Parse Program**: `parse_program_file(path)` creates a `Program` AST
2. **Initialize Compiler**: `compiler_new()` creates a new compilation context
3. **Load VM Params**: Opens `params.inim` (if present) to register global variables
4. **Register VM Globals**: Pre-registers VM globals with stable indices in the compiler
5. **Compile**: `compiler_compile(comp, prog)` generates bytecode from AST
6. **Load & Run**: `vm_load_bytecode(vm, bc)` and `vm_run(vm)` execute the bytecode

## Core APIs

### Parser API (src/parser/parser.h)
```c
Program *parse_program(const char *source);      // Parse from string
Program *parse_program_file(const char *path);   // Parse from file
```

### Compiler API (src/compiler/compiler.h)
```c
Compiler *compiler_new(void);
void compiler_compile(Compiler *comp, Program *prog);
Bytecode *compiler_get_main_bytecode(Compiler *comp);
void compiler_free(Compiler *comp);
```

### VM API (src/vm/vm.h)
```c
void vm_load_bytecode(VM *vm, Bytecode *bc);
void vm_run(VM *vm);
void vm_params_load_v2_or_legacy(VM *vm, const char *path);
```

## Common Errors

### "cannot load bytecode ... (old format? recompile with buildc)"
- **Cause**: `.inim` file predates parameter VM global support
- **Fix**: Recompile using `buildc` or `inimerse -compile` with the original `.im` source

### "cannot read script '...'"
- **Cause**: File not found or unreadable
- **Fix**: Verify path exists and has read permissions

### Parse errors
- **Cause**: Syntax errors in `.im` source
- **Fix**: Review script for syntax issues; error messages point to offending lines

## Building Test Probes

The project includes test probes for individual subsystems:
- `type_registry_probe` - Test type system
- `platform_probe` - Test platform abstractions
- `fiber_probe` - Test fiber/coroutine backend
- `process_probe` - Test process management
- `socket_probe` - Test socket I/O
- `thread_probe` - Test thread management
- `http_probe` - Test HTTP client
- `hub_probe` - Test hub/registry interaction
- `websocket_probe` - Test WebSocket

Run with: `ctest --output-on-failure`

