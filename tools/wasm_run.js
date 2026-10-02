#!/usr/bin/env node
// Inimerse wasm host runner (v0.5 roadmap §2.3): loads a .wasm produced by
// `inimerse compile --abi-target wasm`, provides the fixed env import table
// (docs/WASM.md PAL principle), validates the ABI probe, and runs the script.
//
// Usage: node tools/wasm_run.js <script.wasm>
//        node tools/wasm_run.js --bench <script.wasm>
// Exit code mirrors inimerse run: 0 = ok, 1 = script error (im_error called).
//
// Error codes: 1 division_by_zero, 2 call_frame_overflow, 3 call_stack_overflow,
// 4 heap_exhausted, 5 array_index_out_of_range, 6 array_op_unsupported.
//
// --bench runs the two exported array-sum loops (scalar vs v128) instead of
// the script and prints wall-clock nanoseconds plus an equality check.  See
// docs/WASM.md "SIMD": the vector path is implemented and measured, but the
// .im code generator does not select it - that claim needs a number, so this
// mode is what produces one.

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

function bench(path, inst) {
  // Both loops sum exactly n terms of 1.0, and every partial sum is an exact
  // integer below 2^53, so the scalar and the two-lane result must be equal.
  if (typeof inst.exports.bench_sum_scalar !== 'function' || typeof inst.exports.bench_sum_simd !== 'function') {
    console.error('error: this module has no benchmark exports');
    process.exit(2);
  }
  const n = Number(process.argv[4] || 20000000);
  const runs = [];
  for (const name of ['bench_sum_scalar', 'bench_sum_simd']) {
    const fn = inst.exports[name];
    fn(1000);                                      // warm up / compile
    const t0 = process.hrtime.bigint();
    const v = fn(n);
    const t1 = process.hrtime.bigint();
    runs.push({ name, ns: Number(t1 - t0), v });
  }
  for (const r of runs) {
    if (r.v !== n) { console.error(`error: ${r.name} returned ${r.v}, expected ${n}`); process.exit(1); }
  }
  const [a, b] = runs;
  const ratio = a.ns / b.ns;
  console.log(`wasm bench: n=${n} ${a.name}=${a.ns}ns ${b.name}=${b.ns}ns simd/scalar=${ratio.toFixed(3)}`);
  console.log(`wasm bench: results equal (${a.v} == ${b.v})`);
}

function main() {
  const benchMode = process.argv[2] === '--bench';
  const path = benchMode ? process.argv[3] : process.argv[2];
  if (!path) { console.error('usage: node wasm_run.js [--bench] <script.wasm>'); process.exit(2); }
  const bytes = new Uint8Array(fs.readFileSync(path));

  let exitCode = 0;
  const env = {
    im_print_int: (v) => process.stdout.write(fmtInt(v) + '\n'),
    im_print_float: (d) => process.stdout.write(fmtFloat(d) + '\n'),
    im_print_bool: (v) => process.stdout.write((v ? 'true' : 'false') + '\n'),
    im_print_nil: () => process.stdout.write('nil\n'),
    im_error: (code) => {
      const names = {
        1: 'division_by_zero', 2: 'call_frame_overflow', 3: 'call_stack_overflow',
        4: 'heap_exhausted', 5: 'array_index_out_of_range', 6: 'array_op_unsupported',
      };
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

  if (benchMode) { bench(path, inst); process.exit(0); }

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
