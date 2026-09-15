# Inimerse 0.4.0 Release CI Lessons

## Root cause

`range_meta_runtime` was reported as failed on hosted runners even when the
program printed `range-meta-pass` and exited successfully. The CTest property
used `FAIL_REGULAR_EXPRESSION "range-meta-fail"`. Inimerse prints its compiled
string table for diagnostics, so the source literals for unreachable failure
branches matched that expression and made CTest return `8`.

The test also ran from the project root. That allowed project modules and the
default `params.params` file to participate in a core language test, making
the result depend on the runner environment.

## Permanent fixes

- Use only `PASS_REGULAR_EXPRESSION` for this test. A successful marker is
  sufficient; matching failure literals in program output is unsafe.
- Run `range_meta_runtime` in a CMake-created sandbox with `cmake -E chdir`.
  The core test no longer loads project modules or ambient parameter files.
- Remove the CMake build directory before CI configuration so stale generated
  files cannot affect a release.
- Create the Windows deployment directory before copying `inimerse.exe`.

## Release verification

For a release tag, verify all of the following through the GitHub API:

- `refs/heads/main` and `refs/tags/v0.4.0` point to the intended commit.
- The Release exists, is not draft, and is not prerelease.
- The assets include the Linux tarball, zip archive, Debian package, and
  `SHA256SUMS`.
- Linux, WASI, and Release workflows have completed successfully.

## Operational lesson

When a hosted test disagrees with a local run, expose the direct command and
CTest output in annotations first. Then reduce the test to a clean, isolated
working directory before changing runtime behavior or excluding coverage.
