# Inimerse 0.4.1 CI and Release Lessons

## Windows root cause

The hosted Windows job invoked `build.ps1`, which requires GCC, but the runner
image did not expose a MinGW compiler on `PATH`. Installing MSYS2 alone is not
enough, and hard-coding `C:\msys64` is unreliable because `setup-msys2` can
install under a runner temporary directory.

After GCC was available, the legacy PowerShell source list still failed at
link time because it had drifted from CMake and omitted newer VM, type, result,
process, socket, and speech-stream sources. The full CMake build also exposed a
probe that used POSIX C11 threads directly instead of the project's portable
thread abstraction.

## Permanent Windows pattern

- Install the exact UCRT64 GCC package with `msys2/setup-msys2`.
- Give the setup step an ID and pass its `msys2-location` output to
  `build.ps1` as `MSYS2_LOCATION`.
- Let `build.ps1` discover GCC from that location first, then try standard
  local UCRT64 and MINGW64 paths for developer machines.
- Make `build.ps1` configure and build the CMake target with Ninja instead of
  maintaining a second source and library list. CMake is the canonical Windows
  build graph.
- Reuse that same build directory for CTest in CI instead of compiling the
  engine a second time with a different generator.
- Create `%USERPROFILE%\Infiverse` before copying the executable.
- Preserve complete compiler output on failure while keeping successful CI
  logs compact.

This avoids depending on the hosted image's preinstalled tools, installation
directory, deprecated MINGW64 environment, or a duplicate source list that
silently drifts as the runtime grows.

GitHub-hosted runners also warned when Node 20 actions were forced onto the new
runtime. Keep `actions/checkout` and `actions/setup-node` on their current
Node 24 major versions rather than relying on compatibility shims.

## Test isolation

`range_meta_runtime` and `posix_core_api_runtime` previously used or carried
failure strings that could be echoed by the runtime diagnostic string table.
CTest failure regexes must not match literals that are present in the test
source itself. Core language tests now rely on positive pass markers and run
with `--no-mods` so project modules and ambient parameter files cannot affect
runtime parity checks.

CI removes the CMake build directory before configuration. When a hosted test
fails, the workflow emits both the direct command result and verbose CTest
output as annotations before any test is excluded or runtime behavior changes.
Windows CTest also writes JUnit XML and converts each failed test into a named
GitHub annotation, so diagnosis does not depend on authenticated log access.
Platform-specific tests must be registered or disabled explicitly; a test named
`posix_runtime_parity` is not a Windows release gate. Cross-platform tests run
from CMake-created sandboxes when project modules or parameter files are not
part of the behavior under test.

Thread result/await tests also use a clean sandbox. Loading unrelated project
modules made the full suite intermittently hit the old 15-second timeout even
though the same test completed in under a second when run alone.

The durable thread-await fix was not a timeout change. `thread_await()` builds
Result dictionaries while worker threads may still be active. `vm_dict_set()`
already held the VM global lock, but its insert path called `vm_array_push()`,
which tried to take the same non-recursive lock again whenever
`active_threads > 1`. That self-deadlocked intermittently after the failing
worker recorded its error. Code that runs under `VM_LOCK` must not call helpers
that conditionally acquire `VM_LOCK`; append directly or split the helper into
locked/unlocked variants.

The `--no-mods` flag is now an actual runtime option and thread-focused tests
use it explicitly. Test isolation should be expressed in the command line, not
only by relying on an empty working directory.

The Windows runtime has a separate `runtime.c` implementation, so Linux
coverage alone does not validate its collection and metadata builtins. The
Windows gate caught missing `len(set)` support, an anchored literal matching
edge case, and a process probe command that relied on shell redirection even
though `CreateProcess` does not invoke a shell. Windows thread tests also need
to avoid assuming that a newly created worker has already reached its receive
instruction. Use an explicit ready signal, such as an atomic flag plus bounded
polling, before sending a cross-thread message. Fixed sleeps are host-speed
assumptions, not synchronization.

`--no-mods` must not mean "skip every C module registration." Some C modules
provide core builtins that the compiler already treats as always available,
including thread result helpers and runtime support used by threaded tests.
The durable split is: always register core C modules, skip world/packaging
modules (`infiverse`, `verse_dist`, `build`) and disk-loaded `mods/` for core
tests. That removes startup banners and namespace side effects without changing
VM initialization semantics.

Windows `atomic_get()` and `atomic_set()` must use the same real atomic
read/write semantics as POSIX. A mutex-protected ordinary read can still leave
the test depending on implementation details around thread visibility; use
`InterlockedCompareExchange(..., 0, 0)` for reads and `InterlockedExchange()`
for writes.

Function values crossing a thread message queue are not just integers plus a
function index; their closure payload has ownership. Copying a `VAL_FUNCTION`
for asynchronous delivery should clone the closure function/environment rather
than only sharing a retained pointer. That makes the queued value independent
from the sender register cleanup path and avoids Windows-only lifetime races.

Metadata type tests should assert the type result directly (`x.type == "int"`)
instead of routing a simple equality through regex. Regex behavior is covered
by the portable API test; metadata lowering should fail only when metadata
lowering is wrong.

When a Windows-only failure also prints module load banners, first decide
whether the test is exercising core runtime behavior or module integration.
For core behavior, add `--no-mods` and keep the same pass marker. This does not
weaken the gate; it removes unrelated startup side effects so the shared CTest
suite validates the VM, compiler, and runtime contract directly.

## Release gate

A release is complete only after all of these checks pass:

- Local CMake build and the full CTest suite pass.
- `tools/release_verify.py` accepts the tarball, zip, Debian package,
  `SHA256SUMS`, WASM payload, executable bits, and Debian Python dependency.
- `main` and `v0.4.1` point to the intended commit.
- Linux, Windows, and release GitHub Actions runs succeed for that commit/tag.
- The GitHub Release is public and contains the three versioned Linux packages
  plus `SHA256SUMS`.
- Freshly downloaded release assets pass `sha256sum -c`.

When GitHub CLI is unavailable, use the GitHub REST API for runs, jobs, release
metadata, and asset download. Do not treat a successful upload step or an
existing Release page as proof until the downloaded checksums are verified.
