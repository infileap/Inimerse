# Inimerse WASM ABI probe
> ⚠️ **已归档（2026-10-01）**：本文件已被 [`docs/API.md`](../API.md) / [`docs/STATUS.md`](../STATUS.md) 取代，仅作历史记录保留，结论可能已过时。
> **本次核对到的失效点**：本文件的 `0x0400` 标记属于 `tools/wasm_probe.c` 这套**独立探针**；`--abi-target wasm` 后端使用 `0x0500`。两者不要混用。


The `tools/wasm_probe.c` artifact exports three host-negotiation functions:

- `inimerse_probe()` returns `0x0400` (v0.4 probe marker).
- `inimerse_abi_version()` returns `1` (stable ABI revision).
- `inimerse_capabilities()` returns a capability bitmask; zero means no host capabilities are required.

A WASI or browser host should call these exports before loading a module and reject an unsupported ABI revision.
