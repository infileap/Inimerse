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
// The sign is carried separately, so -0.5 is never printed as "0.5", and a
// fraction that rounds up to 1e6 carries into the integer part, so 0.9999999
// prints "1" rather than "0.1".
function fmtFloat(d) {
  if (Number.isNaN(d)) return 'nan';
  // C's %.17g (what vts_double falls through to for non-finite values) prints
  // "inf"/"-inf"; JS String()/toPrecision() would print "Infinity".
  if (d === Infinity) return 'inf';
  if (d === -Infinity) return '-inf';
  if (d === 0) return '0';
  if (Number.isInteger(d) && Math.abs(d) < 9223372036854775808) return String(BigInt(d));
  if (d >= 1e15 || d <= -1e15) return fmtG17(d);
  const neg = d < 0;
  const a = neg ? -d : d;
  let ip = Math.trunc(a);
  let frac = Math.round((a - ip) * 1e6);
  if (frac === 1000000) { ip += 1; frac = 0; }
  const sign = neg ? '-' : '';
  if (frac === 0) return sign + String(ip);
  const fb = String(frac).padStart(6, '0').replace(/0+$/, '');
  return sign + String(ip) + '.' + fb;
}

// Exact decimal expansion of a positive finite double, as {digits, exp10} where
// digits * 10^exp10 === a.  Every double is m * 2^e; for e < 0 that is m * 5^-e
// scaled by 10^e, so the expansion is finite and BigInt can hold it exactly.
function exactDecimal(a) {
  const buf = new DataView(new ArrayBuffer(8));
  buf.setFloat64(0, a);
  const hi = buf.getUint32(0);
  const lo = buf.getUint32(4);
  const expBits = (hi >>> 20) & 0x7ff;
  let m = (BigInt(hi & 0xfffff) << 32n) | BigInt(lo);
  let e;
  if (expBits === 0) { e = -1074; }              // subnormal
  else { m |= 1n << 52n; e = expBits - 1075; }
  if (e >= 0) return { digits: m << BigInt(e), exp10: 0 };
  return { digits: m * 5n ** BigInt(-e), exp10: e };
}

// C's %.17g, which vts_double uses for |d| >= 1e15 (and for integral values too
// large for int64): 17 significant digits, trailing zeros trimmed, and the
// decimal exponent picks fixed vs scientific (scientific when the exponent is
// < -4 or >= 17, the latter always with at least two digits, so 1e20 -> "1e+20").
//
// Deliberately NOT Number.prototype.toPrecision(17).  That follows ECMAScript's
// tie rule -- "if there are two such n, pick the larger n" -- while glibc rounds
// the exact binary value half-to-even, and the two genuinely disagree on ties.
// 1.0000000000000002e15 is exactly 1000000000000000.25: toPrecision(17) renders
// it "1000000000000000.3", %.17g renders it "...0.2".  A tie needs the exact
// expansion to be 5 followed by nothing, which is why 1e20 and the other pinned
// integers never caught this.
function fmtG17(d) {
  const neg = d < 0;
  const { digits, exp10 } = exactDecimal(neg ? -d : d);
  const L = digits.toString().length;            // exact digit count
  let exp = L - 1 + exp10;                       // floor(log10(|d|))
  let n = digits;
  const k = L - 17;
  if (k > 0) {
    const p = 10n ** BigInt(k);
    let q = n / p;
    const r = n % p;
    const twice = r * 2n;
    if (twice > p || (twice === p && q % 2n === 1n)) q += 1n;   // half-to-even
    n = q;
  } else if (k < 0) {
    n = n * 10n ** BigInt(-k);
  }
  if (n === 10n ** 17n) { n = 10n ** 16n; exp += 1; }           // carry out
  const s = n.toString();                        // exactly 17 digits
  if (exp < -4 || exp >= 17) {
    const mant = (s.slice(0, 1) + '.' + s.slice(1)).replace(/0+$/, '').replace(/\.$/, '');
    return (neg ? '-' : '') + mant + 'e' + (exp < 0 ? '-' : '+') + String(Math.abs(exp)).padStart(2, '0');
  }
  const pointAt = exp + 1;
  let out;
  if (pointAt <= 0) out = '0.' + '0'.repeat(-pointAt) + s;
  else if (pointAt >= 17) out = s + '0'.repeat(pointAt - 17);
  else out = s.slice(0, pointAt) + '.' + s.slice(pointAt);
  if (out.indexOf('.') !== -1) out = out.replace(/0+$/, '').replace(/\.$/, '');
  return (neg ? '-' : '') + out;
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
        7: 'numeric_overflow',
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
