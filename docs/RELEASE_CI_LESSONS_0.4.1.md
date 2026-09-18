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

`range_meta_runtime` previously used a failure regex that matched source
literals printed in the runtime diagnostic string table. It now relies on a
positive pass marker and runs in a CMake-created sandbox so project modules and
ambient parameter files cannot affect a core language test.

CI removes the CMake build directory before configuration. When a hosted test
fails, the workflow emits both the direct command result and verbose CTest
output as annotations before any test is excluded or runtime behavior changes.
Windows CTest also writes JUnit XML and converts each failed test into a named
GitHub annotation, so diagnosis does not depend on authenticated log access.

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
