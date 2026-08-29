'use strict';

/* Browser/WASI-neutral host boundary for Inimerse WebAssembly modules.
 * The host owns capabilities; a module receives only the imports explicitly
 * supplied by the caller.  No filesystem, network, or DOM APIs are exposed
 * implicitly. */
class InimerseWasmHost {
  constructor(options = {}) {
    this.maxPages = Number.isInteger(options.maxPages) && options.maxPages > 0 ? options.maxPages : 256;
    this.imports = { env: {} };
    if (typeof options.nowMs === 'function') this.imports.env.inimerse_now_ms = options.nowMs;
    this.instance = null;
    this.module = null;
  }

  async load(bytes) {
    if (!(bytes instanceof ArrayBuffer) && !ArrayBuffer.isView(bytes)) throw new TypeError('wasm bytes required');
    const module = await WebAssembly.compile(bytes);
    const imports = WebAssembly.Module.imports(module);
    for (const item of imports) {
      if (!this.imports[item.module] || typeof this.imports[item.module][item.name] !== 'function')
        throw new Error(`unsupported WASM import: ${item.module}.${item.name}`);
    }
    const result = await WebAssembly.instantiate(module, this.imports);
    this.module = module;
    this.instance = result;
    const abi = this.instance.exports.inimerse_abi_version;
    if (typeof abi !== 'function' || Number(abi()) !== 1) throw new Error('unsupported Inimerse WASM ABI');
    return this;
  }

  capabilities() {
    if (!this.instance) throw new Error('WASM module is not loaded');
    const fn = this.instance.exports.inimerse_capabilities;
    return typeof fn === 'function' ? Number(fn()) >>> 0 : 0;
  }

  probe() {
    if (!this.instance) throw new Error('WASM module is not loaded');
    const fn = this.instance.exports.inimerse_probe;
    if (typeof fn !== 'function') throw new Error('missing inimerse_probe export');
    return Number(fn());
  }
}

module.exports = { InimerseWasmHost };
