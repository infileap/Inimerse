# Inimerse 0.4.1 Release Notes

Inimerse 0.4.1 is a patch release for the 0.4 portable runtime line.

## Fixes

- Fixed hosted CI false failures in the range metadata regression by running
  the core language test from an isolated CMake sandbox.
- Removed the unsafe CTest failure regex that matched diagnostic string-table
  output instead of executed program output.
- Made CI CMake builds start from a clean build directory.
- Fixed Windows CI setup so the legacy `build.ps1` path has a MinGW toolchain
  available on GitHub hosted runners.
- Made `build.ps1` use CMake as the single canonical Windows source list,
  preventing linker failures when new runtime modules are added.
- Made the closure concurrency probe use the project's portable thread layer,
  so the full probe set builds with MinGW as well as POSIX toolchains.
- Updated GitHub's checkout and Node setup actions to their Node 24 releases.
- Made `build.ps1` create the local deployment directory before copying the
  Windows executable.

## Assets

This release publishes the Linux runtime packages:

- `inimerse-0.4.1-Linux-x86_64.tar.gz`
- `inimerse-0.4.1-Linux-x86_64.zip`
- `inimerse-0.4.1-Linux-x86_64.deb`
- `SHA256SUMS`
