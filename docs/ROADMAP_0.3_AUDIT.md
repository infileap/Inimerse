# v0.3 roadmap audit (2026-08-29)

This document separates the original unchecked boxes from work that is
actually required for a release. The original roadmap contains 25 unchecked
boxes, but several are stale: the POSIX PAL, isolate process boundary, basic
say targets, package signing/preview, and the local workbench already have
code and passing probes.

## Verified complete

- Make and CMake builds succeed on Linux; CTest passes 13/13 tests.
- Windows CI builds the engine and desktop application; WASI probe and host
  boundary are built and tested.
- Fiber, thread, process, directory, socket, HTTP, WebSocket and CRP-session
  probes pass. `server_mod` port checks now use the socket PAL.
- `isolate_run` has Windows and POSIX implementations with output capture,
  timeout and memory/CPU limits.
- UPP/CRP reference tests, `.vverse` validation/packing, Hub client cache and
  multi-target say regression tests pass.
- Tauri commands for local project discovery, editing, save/run/stop,
  diagnostics, package preview/install/remove and Repair are present.

## Genuine v0.3 gaps

1. **One networking PAL for all backends.** Server socket operations and
   POSIX runtime HTTP GET/POST now use the shared socket/HTTP PAL (with a
   deterministic HTTP probe). LAN address discovery is now in the platform
   PAL; `server_mod_posix` now uses PAL file read/write, directory and LAN
   discovery helpers;
   runtime `exec` now uses the process
   PAL's bounded capture/timeout path,
   while serial access now goes through the serial PAL. Key/mouse operations remain explicit unsupported
   capabilities. Windows and POSIX server implementations are not one shared
   backend yet.
2. **Integrated UPP/CRP service.** Native HTTP now persists peer/session
   state, enforces scoped capabilities, and exposes `POST/GET /route` for
   durable peer routing. `CrpClient` exposes `registerRoute`/`resolveRoute`,
   covered by relay regression. Both reference relay and native service now
   retain bounded event history (native history is persisted) and support replay-enabled resume. NAT
   traversal now has candidate exchange endpoints (`/nat/candidate`,
   `/nat/candidates`); native and reference clients now expose authenticated
   resume/stop operations, with client heartbeat support over signal.
3. **Authorization and persistence.** Portal tokens expire/revoke and are
   scoped to Verse/peer; the JS client now propagates those credentials.
   Friend/session/route registries persist on disk. Content capability scopes,
   scope mismatch/expiry behavior is covered by the native HTTP probe;
   signature/version/replay coverage still needs broader native end-to-end tests.
4. **Hub URI desktop closure.** Local `.vverse` discovery/install/run works;
   `verse://hub/<id>` discovery, download, signature verification and launch
   are not wired through the Tauri UI.
5. **IDE finish.** The editor remains a textarea with line tracking rather than
   syntax highlighting. Diagnostics now run live (debounced) through the
   `workbench_check` command and share the same rendered model as run errors;
   the Stop control is wired into the toolbar.
6. **Production OutputStream.** The C API now provides synchronous bounded
   writes (`output_stream_open/write/close`), explicit cancellation/status,
   format/priority metadata, flush and transport-error reporting. An
   asynchronous queue, character dialogue metadata and an enforced AI/player
   stream boundary remain for the production follow-up. The native API now
   exposes a fixed FIFO enqueue/flush path, requires explicit `source=ai`
   metadata for AI-target streams, and preserves dialogue/character metadata
   through synchronous and queued delivery.

## Explicitly deferred

The full `wasm32-wasi` VM and browser import table are explicitly described as
outside the v0.3 release scope in `ROADMAP_0.3_STATUS.md`; only the toolchain
probe belongs to v0.3. Collection types, BigInt/enum narrowing, and `type`
declarations belong to v3.1.

Therefore the honest release status is: **a small set of concrete gap groups remain**;
the original count of 25 is a checklist count, not a count of missing
features. Completing the six groups (or formally moving the deferred items to
the next roadmap) is required before calling v0.3 finished.

## Current verification snapshot (2026-08-30)

- CMake native build and CTest: 13/13 passed.
- JavaScript protocol regression: 10/10 passed.
- CPack Linux TGZ, ZIP and DEB packages generated successfully.
- Native HTTP/CRP now covers scoped tokens, persistence, route/candidate
  exchange, session start/heartbeat/stop, bounded event replay and HTTP PAL.
