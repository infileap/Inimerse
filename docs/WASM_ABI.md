# Inimerse WASM ABI probe

The `tools/wasm_probe.c` artifact exports three host-negotiation functions:

- `inimerse_probe()` returns `0x0300` (v0.3 probe marker).
- `inimerse_abi_version()` returns `1` (stable ABI revision).
- `inimerse_capabilities()` returns a capability bitmask; zero means no host capabilities are required.

A WASI or browser host should call these exports before loading a module and reject an unsupported ABI revision.
