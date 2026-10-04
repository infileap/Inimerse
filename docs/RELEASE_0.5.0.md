# Inimerse 0.5.0 Release Notes

Inimerse 0.5.0 is the first release of the 0.5 line, 182 commits on from 0.4.1.
It is a correctness release: the language and the two runtimes were made to
agree with each other, the verse (P1) layer gained a process-boundary closed
loop, and the Windows build and its full CTest suite were brought back up.

Baseline for this release (see `docs/STATUS.md` §2):

| | |
| --- | --- |
| Version | `0.5.0` (`CMakeLists.txt`) |
| Clean build | configure and build exit 0, **0 errors** |
| Full test suite | **123 / 123** CTest cases on Linux; 103 of the same cases run on Windows (see the Windows section below) |
| Windows | the suite is green under MinGW-w64/UCRT64, in CI and locally (see below) |

## Language and runtime

- `Value`'s integer slot is 64-bit end to end, and the differential fuzzer's
  remaining interpreter/native divergences reach zero.
- `a + "text"` no longer drops its left operand, and a `+` chain answers the
  same whether it has two, three or four terms.
- Truthiness has a single production point, so a condition can no longer be
  evaluated one way by the compiler and another by the VM.
- Boolean `and`/`or` are value-returning and agree across backends; `%` is
  64-bit; `--lint` reports a diagnosable form of the same checks.
- Sets: a set literal is a union of its intervals, enumeration walks all of it,
  `str()` and `sum()` count interval components rather than literals, and
  `list(<set>)` hands back values instead of a handle.
- One float format everywhere, including `str()` and the AOT backend.
- **Ordering refuses instead of inventing an answer.** A pair that has no order
  (`"abc" < 1`, `nil <= 0`) raises `type_mismatch`; it previously answered `0`
  and made the comparison true.
- `sum()` means the same thing on Windows and on POSIX, and accumulates in 64
  bits.
- `atomic_add`/`atomic_set` are 64-bit on a 64-bit slot. The Windows
  implementation used the 32-bit interlocked forms and silently truncated;
  overflow now raises `numeric_overflow` and leaves the slot untouched.
- On Windows, a boolean read the union's `double` member, and `len()`/`size()`
  had no fallback when the collection count was absent.
- An unknown builtin is now an error instead of silently consuming the last
  argument, and the `L_CALL_FUNC` bounds check runs for every function rather
  than only for functions named `h`.
- `vm_init` zeroes the VM struct it is given. Twelve fields were never assigned
  and were read as stack garbage; on Windows this crashed 18 CTest cases.
- Sixteen `snprintf` truncation sites were fixed, and a path that does not fit
  is now refused instead of silently truncated.

## Verse layer (P1)

- The P1 closed loop — create, enter, sync, drain, recover, undo, replay — is
  driven across a real process boundary (`inim-server`/`inim-client` child
  processes over pipes) by CTest `verse_closed_loop`, so it cannot pass by
  accident through shared memory.
- Deterministic replay closure: a fresh process rebuilds the same sequence
  number, chain head and cell state from the on-disk layer.
- Engine-side UPP framing and the CRP session state machine have their own
  probes and cross-checks against reference frames (`verse_upp_probe`,
  `verse_crp_probe`, and the JS crosscheck suites).

## Platform

- **Windows builds again.** A repo header shadowed a CRT header, which broke the
  MinGW build outright.
- The CTest suite runs on Windows: the harnesses no longer assume a POSIX
  `python3` name or a `build/` directory layout, subprocess output is decoded as
  UTF-8 instead of the ANSI codepage, `inimerse.exe`/`aot-native.exe` are found
  by name, and CRLF differences no longer fail the frame cross-checks.
- The Linux build workflow and the release workflow called a `Makefile` target
  that no longer exists; both now use CMake, and the release workflow derives
  the version from the tag and refuses to publish if `CMakeLists.txt` disagrees.

## Windows

The Windows asset is new in this release. Its engine is the same source as the
Linux one, with two deliberate platform differences:

- **The distributed hub is reduced.** The complete hub — `/package`,
  `/package/fork`, `/content/`, `/economy/*`, `/node/*`, `/session/*` — is
  `src/platform/http_posix.c`, which the source lists compile into the POSIX
  engine only. The Windows engine links `src/mod/verse_dist_mod.c` instead: a
  reduced hub that answers `/ping`, `/v/<id>`, `/hub` and `/api/forge`. Local
  verses, the VDP (`verse://local/…`), `inim` and `aot-native` are unaffected.
- **The xlang bridges are not built**, so `xlang_python_bridge` and
  `xlang_java_bridge` skip (they exit 77 and CTest reports them as skipped).

Nine CTest regressions drive the POSIX hub's routes, so on Windows they are
disabled rather than failed (`DISABLED TRUE` in `CMakeLists.txt`), and the suite
reports the Windows engine's own coverage instead of failing on a feature that
engine does not ship. `posix_runtime_parity` has been disabled the same way
since before 0.5.0. Nothing is skipped silently: every disabled case is listed
by `ctest -N` and named in this file.

The suite is green on both platforms for the commit this release is built from:
`123 / 123` on Linux, and on Windows `103` run of `113` registered — the ten
`DISABLED` cases above plus the two bridge skips, no failures. The Windows run
is what CI executes on every push (`windows-build.yml`, MSYS2 UCRT64), and the
Windows assets below are built from that same commit with the same toolchain.

## Assets

Linux x86_64:

- `inimerse-0.5.0-Linux-x86_64.tar.gz`
- `inimerse-0.5.0-Linux-x86_64.zip`
- `inimerse-0.5.0-Linux-x86_64.deb`
- `SHA256SUMS`

Windows x86_64:

- `inimerse-0.5.0-Windows-x86_64.zip` — the engine and its tools, built with
  MinGW-w64/UCRT64. It holds `bin/inimerse.exe`, `bin/aot-native.exe` (the AOT
  translator for the numeric subset) and `bin/inim.py` (the `inim` driver; the
  `.py` suffix is what Windows needs to run it).
- `InfiverseSetup-0.5.0.exe` — the Infiverse desktop installer (Inno Setup 7),
  bundling the engine, the desktop shell and the standard plugin set.
- `SHA256SUMS-Windows` — the two hashes above, in `sha256sum -c` format.
