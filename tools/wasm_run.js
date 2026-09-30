#!/usr/bin/env node
// Inimerse wasm host runner (v0.5 roadmap §2.3): loads a .wasm produced by
// `inimerse compile --abi-target wasm`, provides the fixed env import table
// (docs/WASM.md PAL principle), validates the ABI probe, and runs the script.
//
// Usage: node tools/wasm_run.js <script.wasm>
// Exit code mirrors inimerse run: 0 = ok, 1 = script error (im_error called).

const fs = require('fs');

function fmtInt(v) { return String(v); }

// Mirrors vts_double() in src/vm/vm.c: nan -> "nan", 0 -> "0", integral
// values within int64 print as integers, >= 1e15 uses %.17g, otherwise
// integer part + up to 6 fractional digits with trailing zeros trimmed.
function fmtFloat(d) {
  if (Number.isNaN(d)) return 'nan';
  if (d === 0) return '0';
  if (Number.isInteger(d) && Math.abs(d) < 9223372036854775808) return String(BigInt(d));
  if (d >= 1e15 || d <= -1e15) return d.toPrecision(17).replace(/(\.\d*?)0+($|e)/, '$1$2').replace(/\.$/, '');
  const neg = d < 0;
  const a = neg ? -d : d;
  const ip = Math.trunc(a);
  const frac = Math.round((a - ip) * 1e6);
  let out = String(ip);
  if (frac === 0) return neg ? '-' + out : out;
  let fb = String(frac).padStart(6, '0');
  fb = fb.replace(/0+$/, '');
  if (fb === '') return neg ? '-' + out : out;
  return (neg ? '-' : '') + out + '.' + fb;
}

function main() {
  const path = process.argv[2];
  if (!path) { console.error('usage: node wasm_run.js <script.wasm>'); process.exit(2); }
  const bytes = new Uint8Array(fs.readFileSync(path));

  let exitCode = 0;
  const env = {
    im_print_int: (v) => process.stdout.write(fmtInt(v) + '\n'),
    im_print_float: (d) => process.stdout.write(fmtFloat(d) + '\n'),
    im_print_bool: (v) => process.stdout.write((v ? 'true' : 'false') + '\n'),
    im_print_nil: () => process.stdout.write('nil\n'),
    im_error: (code) => {
      const names = { 1: 'division_by_zero', 2: 'call_frame_overflow', 3: 'call_stack_overflow' };
      console.error(`error: ${names[code] || 'runtime_error'} (code ${code})`);
      exitCode = 1;
    },
  };

  const mod = new WebAssembly.Module(bytes);
  const inst = new WebAssembly.Instance(mod, { env });

  // ABI negotiation (docs/WASM_ABI.md): probe before running.
  const probe = inst.exports.inimerse_probe();
  const abi = inst.exports.inimerse_abi_version();
  if (probe !== 0x0500) { console.error(`error: unexpected wasm probe marker 0x${probe.toString(16)}`); process.exit(2); }
  if (abi !== 1) { console.error(`error: unsupported wasm ABI revision ${abi}`); process.exit(2); }

  try {
    inst.exports.inimerse_run(0);
  } catch (e) {
    // wasm trap (e.g. strings in the MVP subset): report like the interpreter
    console.error(`error: ${String(e.message || e)}`);
    process.exit(1);
  }
  process.exit(exitCode);
}

main();
