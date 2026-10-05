cmake_minimum_required(VERSION 3.20)
project(inimerse LANGUAGES C)
enable_testing()

set(CPACK_PACKAGE_NAME "inimerse")
set(CPACK_PACKAGE_VENDOR "Infileap")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Portable Inimerse VM")
set(INIMERSE_PACKAGE_VERSION "0.5.0") # v0.5: compiled runtime platform CACHE STRING "Package/release version")
set(CPACK_PACKAGE_VERSION "${INIMERSE_PACKAGE_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "inimerse-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ;ZIP;DEB")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "python3, python3-cryptography")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Infileap")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
include(CPack)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# The VM now has a functional POSIX backend; build the portable engine by
# default on every supported host. Platform-specific modules remain gated.
option(INIMERSE_BUILD_ENGINE "Build the full VM engine" ON)

add_library(inimerse_platform STATIC src/platform/platform.c src/platform/thread.c src/platform/fiber.c src/platform/process.c src/platform/socket.c src/platform/dir.c src/platform/vfs.c)
target_include_directories(inimerse_platform PUBLIC src)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(inimerse_platform PUBLIC Threads::Threads)
endif()

add_executable(platform_probe src/platform/platform_probe.c)
target_link_libraries(platform_probe PRIVATE inimerse_platform)
add_executable(vfs_probe src/platform/vfs_probe.c)
target_link_libraries(vfs_probe PRIVATE inimerse_platform)
add_executable(jit_mode_probe src/vm/jit_mode_probe.c src/vm/jit_mode.c)
target_include_directories(jit_mode_probe PRIVATE src/vm)
add_test(NAME jit_mode_probe COMMAND jit_mode_probe)
# P1 increment 1: Layer event log / commit contract (canonical JSON, state_hash,
# durable-flush barrier, RECOVERY_REQUIRED, idempotency).
add_executable(verse_eventlog_probe
  src/verse/eventlog_probe.c src/verse/eventlog.c src/common/sha256.c)
target_include_directories(verse_eventlog_probe PRIVATE src/verse src/common)
add_test(NAME verse_eventlog_probe COMMAND verse_eventlog_probe)
# P1 increment 2: authoritative Layer (verse_id + manifest, commit, idempotency,
# undo, snapshot, deterministic replay).
add_executable(verse_layer_probe
  src/verse/layer_probe.c src/verse/layer.c src/verse/eventlog.c
  src/verse/json_min.c src/common/sha256.c)
target_include_directories(verse_layer_probe PRIVATE src/verse src/common)
add_test(NAME verse_layer_probe COMMAND verse_layer_probe)
# P1 increment 3: Layer session protocol (hello/version+capability negotiation,
# client-authority refusal, drain barrier) and the inim-server process boundary.
set(VL_CORE_SOURCES
  src/verse/eventlog.c src/verse/json_min.c src/verse/layer.c
  src/verse/protocol.c src/common/sha256.c)
add_executable(verse_protocol_probe src/verse/protocol_probe.c ${VL_CORE_SOURCES})
target_include_directories(verse_protocol_probe PRIVATE src/verse src/common)
add_test(NAME verse_protocol_probe COMMAND verse_protocol_probe)
add_executable(inim-server src/verse/server.c ${VL_CORE_SOURCES})
target_include_directories(inim-server PRIVATE src/verse src/common)
add_executable(inim-client src/verse/client.c)
# Locate the test interpreter before the first add_test that needs it.  The
# interpreter is not called python3 everywhere: on Windows it is python.exe, and
# a hardcoded `python3` makes the test die with "no such file or directory".
find_program(INIMERSE_PYTHON NAMES python3 python)
if(INIMERSE_PYTHON)
  set(INIMERSE_TEST_PYTHON "${INIMERSE_PYTHON}")
else()
  set(INIMERSE_TEST_PYTHON "python3")
endif()
add_test(NAME verse_closed_loop
  COMMAND ${CMAKE_COMMAND} -E env INIM_SERVER_BIN=$<TARGET_FILE:inim-server>
          ${INIMERSE_TEST_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verse_closed_loop.test.py
          $<TARGET_FILE:inim-client>)
# P1 increment 4: engine-side UPP -- universal plugin protocol framing (JSONL,
# 1 MiB frame cap, hello/welcome negotiation) and the session state machine.
# src/verse/upp_probe.c asserts the boundary cases offline; the
# verse_upp_crosscheck test registered below replays one corpus through both
# this engine implementation and the JS reference and diffs the transcripts.
add_executable(verse_upp_probe
  src/verse/upp_probe.c src/verse/upp.c src/verse/json_min.c)
target_include_directories(verse_upp_probe PRIVATE src/verse src/common)
add_test(NAME verse_upp_probe COMMAND verse_upp_probe)
set_tests_properties(verse_upp_probe PROPERTIES TIMEOUT 30 LABELS "protocol;upp")
# json_min's `\u` decoder: surrogate pairs must combine into one 4-byte UTF-8
# sequence, lone surrogates must become U+FFFD, and \u0000 must be refused
# rather than truncated at a raw NUL in a length-less char *.  It links upp.c
# so the round-trip half uses the shipped writer, not a copy of it.  See
# docs/streams/json-min-nul-escape.md.
add_executable(verse_json_min_probe
  src/verse/json_min_probe.c src/verse/json_min.c src/verse/upp.c)
target_include_directories(verse_json_min_probe PRIVATE src/verse src/common)
add_test(NAME verse_json_min_probe COMMAND verse_json_min_probe)
set_tests_properties(verse_json_min_probe PROPERTIES TIMEOUT 30 LABELS "protocol;json")
# P1 increment 5: engine-side CRP -- the FIND / PORTAL / SIGNAL wire frames, the
# base64url + HMAC-SHA256 capability token, and the FIND registry / PORTAL
# issuance that until now only existed in tools/crp_reference.js and
# tools/crp_relay.js.  src/verse/crp_probe.c asserts the boundary cases offline.
# crp-hub and crp-peer are two real processes: the hub owns the registry and
# listens on a socket, the peer walks the relay scenarios over it, and
# tools/crp_closed_loop.test.py drives both.  The verse_crp_crosscheck test
# registered below replays one corpus through this engine implementation and the
# JS reference and diffs the two transcripts.
add_executable(verse_crp_probe
  src/verse/crp_probe.c src/verse/crp.c src/verse/upp.c src/verse/json_min.c
  src/platform/crp_session.c src/common/sha256.c)
target_include_directories(verse_crp_probe PRIVATE src/verse src/common src/platform)
add_test(NAME verse_crp_probe COMMAND verse_crp_probe)
set_tests_properties(verse_crp_probe PROPERTIES TIMEOUT 60 LABELS "protocol;crp")
if(NOT WIN32)
  add_executable(crp-hub
    src/verse/crp_hub.c src/verse/crp.c src/verse/upp.c src/verse/json_min.c
    src/platform/crp_session.c src/common/sha256.c)
  target_include_directories(crp-hub PRIVATE src/verse src/common src/platform)
  add_executable(crp-peer
    src/verse/crp_peer.c src/verse/crp.c src/verse/upp.c src/verse/json_min.c
    src/platform/crp_session.c src/common/sha256.c)
  target_include_directories(crp-peer PRIVATE src/verse src/common src/platform)
  add_test(NAME verse_crp_closed_loop
    COMMAND ${CMAKE_COMMAND} -E env INIM_CRP_HUB_BIN=$<TARGET_FILE:crp-hub>
            ${INIMERSE_TEST_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/crp_closed_loop.test.py
            $<TARGET_FILE:crp-peer>)
  set_tests_properties(verse_crp_closed_loop PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60 LABELS "protocol;crp")
endif()
# Ed25519 sign/verify round-trip probe.  Existing suites only sign a handful of
# times each, so the 1/32 carry-propagation defect in ed25519_sign (fixed in
# src/common/ed25519.c) went unnoticed; this signs 2000 deterministic messages.
add_executable(ed25519_probe src/common/ed25519_probe.c src/common/ed25519.c)
target_include_directories(ed25519_probe PRIVATE src/common)
add_test(NAME ed25519_probe COMMAND ed25519_probe)
# Engine-side `.vverse` packer (src/common/vverse_pack.c + src/common/gzip.c).
# The format is owned by the read-only JS reference tools/vverse_pack.js, so
# this probe locks what the reference cannot check for us: byte-level
# determinism (same tree -> same sha256, mtime pinned to 0), purity (packing
# must not write into the source tree), the ed25519 path over a >8 KiB payload,
# tamper detection in both directions of the digest table, path-escape refusal,
# and gzip known answers produced by zlib itself (dynamic Huffman + FNAME).
add_executable(vverse_pack_probe
  src/common/vverse_pack_probe.c src/common/vverse_pack.c src/common/gzip.c
  src/common/sha256.c src/common/ed25519.c src/verse/json_min.c)
target_include_directories(vverse_pack_probe PRIVATE src/verse src/common src/platform)
target_link_libraries(vverse_pack_probe PRIVATE inimerse_platform)
add_test(NAME vverse_pack_probe COMMAND vverse_pack_probe)
add_executable(bytecode_capture_probe src/compiler/bytecode.c src/compilation/deps.c src/compiler/bytecode_capture_probe.c)
target_include_directories(bytecode_capture_probe PRIVATE src src/compiler src/common)
target_link_libraries(bytecode_capture_probe PRIVATE inimerse_platform)
add_test(NAME bytecode_capture_probe COMMAND bytecode_capture_probe)
add_executable(closure_probe src/vm/closure.c src/vm/closure_probe.c src/platform/thread.c)
target_include_directories(closure_probe PRIVATE src/vm src src/compiler src/common)
if(NOT WIN32)
  target_link_libraries(closure_probe PRIVATE Threads::Threads)
endif()
add_test(NAME closure_probe COMMAND closure_probe)

add_executable(typeset_probe src/types/typeset.c src/types/typeset_probe.c)
target_include_directories(typeset_probe PRIVATE src/types)
add_test(NAME typeset_probe COMMAND typeset_probe)
add_executable(enum_probe src/types/typeset.c src/types/enum.c src/types/enum_probe.c)
target_include_directories(enum_probe PRIVATE src/types)
add_test(NAME enum_probe COMMAND enum_probe)
add_executable(error_types_probe src/types/typeset.c src/types/enum.c src/types/error_types.c src/types/error_types_probe.c)
target_include_directories(error_types_probe PRIVATE src/types)
add_test(NAME error_types_probe COMMAND error_types_probe)
add_executable(type_registry_probe src/types/typeset.c src/types/enum.c src/types/registry.c src/types/registry_probe.c)
target_include_directories(type_registry_probe PRIVATE src/types)
add_test(NAME type_registry_probe COMMAND type_registry_probe)

# These six probes state their expectations with assert(), and assert() is a
# no-op under -DNDEBUG -- which is exactly what CMAKE_C_FLAGS_RELEASE carries
# (-O3 -DNDEBUG).  Compiled as-is they exit 0 no matter what the engine does,
# so CTest reports a green test that checks nothing at all.  -UNDEBUG has to
# come after -DNDEBUG on the command line; target_compile_options appends
# there, whereas target_compile_definitions would not undo it.
foreach(_asserting_probe
    bytecode_capture_probe closure_probe typeset_probe
    enum_probe error_types_probe type_registry_probe)
  target_compile_options(${_asserting_probe} PRIVATE -UNDEBUG)
endforeach()

add_executable(fiber_probe src/platform/fiber.c src/platform/fiber_probe.c)
if(NOT WIN32)
  target_link_libraries(fiber_probe PRIVATE Threads::Threads)
endif()
add_executable(process_probe src/platform/process.c src/platform/process_probe.c)
if(NOT WIN32)
  add_executable(crp_session_probe src/platform/crp_session.c src/common/sha256.c src/platform/crp_session_probe.c)
  add_test(NAME crp_session_probe COMMAND crp_session_probe)
endif()
add_executable(socket_probe src/platform/socket.c src/platform/thread.c src/platform/socket_probe.c)
if(WIN32)
  target_link_libraries(socket_probe PRIVATE ws2_32)
else()
  target_link_libraries(socket_probe PRIVATE Threads::Threads)
endif()
add_executable(thread_probe src/platform/thread.c src/platform/thread_probe.c)
if(NOT WIN32)
  target_link_libraries(thread_probe PRIVATE Threads::Threads)
endif()
add_test(NAME platform_probe COMMAND platform_probe)
add_test(NAME fiber_probe COMMAND fiber_probe)
add_test(NAME process_probe COMMAND process_probe)
add_test(NAME socket_probe COMMAND socket_probe)
add_test(NAME thread_probe COMMAND thread_probe)
add_executable(dir_probe src/platform/dir.c src/platform/dir_probe.c)
add_test(NAME dir_probe COMMAND dir_probe)
if(NOT WIN32)
  add_executable(headless_probe src/headless_server_posix.c src/headless_server_probe.c src/platform/socket.c src/platform/platform.c src/platform/thread.c)
  target_link_libraries(headless_probe PRIVATE Threads::Threads)
  add_test(NAME headless_probe COMMAND headless_probe)
endif()
if(NOT WIN32)
  add_executable(http_probe src/platform/socket.c src/platform/thread.c src/platform/dir.c src/platform/websocket.c src/platform/crp_session.c src/common/sha256.c src/platform/http_posix.c src/verse/json_min.c src/verse/crp.c src/verse/upp.c src/common/ed25519.c src/platform/http_client.c src/platform/http_probe.c)
  target_link_libraries(http_probe PRIVATE Threads::Threads)
  target_include_directories(http_probe PRIVATE src/verse src/common src/platform)
  add_test(NAME http_probe COMMAND bash ${CMAKE_SOURCE_DIR}/tools/http_probe_retry.sh $<TARGET_FILE:http_probe>)
  add_executable(hub_probe src/platform/socket.c src/platform/thread.c src/platform/dir.c src/platform/websocket.c src/platform/crp_session.c src/common/sha256.c src/platform/http_posix.c src/verse/json_min.c src/verse/crp.c src/verse/upp.c src/common/ed25519.c src/platform/http_client.c src/platform/hub_probe.c)
  target_link_libraries(hub_probe PRIVATE Threads::Threads)
  target_include_directories(hub_probe PRIVATE src/verse src/common src/platform)
  add_test(NAME hub_probe COMMAND hub_probe)
  add_executable(websocket_probe src/platform/socket.c src/platform/thread.c src/platform/dir.c src/platform/websocket.c src/platform/crp_session.c src/common/sha256.c src/platform/http_posix.c src/verse/json_min.c src/verse/crp.c src/verse/upp.c src/common/ed25519.c src/platform/http_client.c src/platform/websocket_probe.c)
  target_link_libraries(websocket_probe PRIVATE Threads::Threads)
  target_include_directories(websocket_probe PRIVATE src/verse src/common src/platform)
endif()
set_tests_properties(platform_probe fiber_probe process_probe socket_probe thread_probe dir_probe
  PROPERTIES TIMEOUT 10 LABELS "platform;smoke" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
if(NOT WIN32)
  set_tests_properties(headless_probe PROPERTIES TIMEOUT 10 LABELS "platform;smoke;headless")
  set_tests_properties(http_probe hub_probe PROPERTIES TIMEOUT 30 LABELS "platform;smoke;http" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(crp_session_probe PROPERTIES TIMEOUT 10 LABELS "platform;smoke;crp")
endif()
find_program(INIMERSE_NODE node)
# INIMERSE_PYTHON is located above, before the first add_test that needs it.
if(INIMERSE_PYTHON)
  add_test(NAME inim_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/inim.test.py)
  set_tests_properties(inim_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "package;regression")
  add_test(NAME cli_incremental_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/cli_incremental.test.py)
  set_tests_properties(cli_incremental_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "compiler;regression")
  add_test(NAME selfhost_benchmark COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/selfhost_bench.py --runs 1)
  set_tests_properties(selfhost_benchmark PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "benchmark;regression")
  # Guard against the self-host toolchain rotting unnoticed.  Nothing in the suite used to touch
  # selfhost/*.im (`grep -rn "compiler\.im" tools/ CMakeLists.txt` was empty), which is how
  # selfhost/compiler.im stayed unparseable from 2026-09-06 until 2026-10-02 while the gate
  # stayed green.  compiler.im chdirs to its own directory, so the target path is relative to
  # selfhost/ even though WORKING_DIRECTORY is the source root.
  # This asserts only that compiler.im PARSES and reaches code emission (the `main:` listing).
  # It deliberately does NOT assert that the emitted bytecode is correct: compile_program()
  # currently emits a bare OP_HALT for every program (see the open BOARD row).
  add_test(NAME selfhost_toolchain_parses COMMAND inimerse ${CMAKE_SOURCE_DIR}/selfhost/compiler.im --dump tests/_mini.im)
  set_tests_properties(selfhost_toolchain_parses PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "compiler;selfhost;regression" PASS_REGULAR_EXPRESSION "main:")
  # C path vs self-host path: the same source must produce the same *behaviour*.
  # This is the regression for BOARD row `selfhost-codegen-empty`, and it cannot
  # be an exit-code assertion -- both paths exit 0 even when the self-host path
  # prints nothing at all, which is exactly what happened: `vm_exec` was
  # registered as an unsupported stub on POSIX, and selfhost/compiler.im never
  # emitted the implicit OP_RETURN that src/compiler/compiler.c:379 emits at the
  # end of every function body.  The two compilers disagree on register
  # allocation, so bytecode hashes are only *recorded*; what is asserted is that
  # both paths print the same bytes.
  add_test(NAME selfhost_codegen_parity
           COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/selfhost_compare.py --check --engine $<TARGET_FILE:inimerse>)
  set_tests_properties(selfhost_codegen_parity PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 LABELS "compiler;selfhost;regression")
  # The parity run spawns the engine once per target, and a process spawn on
  # Windows costs an order of magnitude more than on Linux: the same 33 targets
  # take 7 s here on Linux and 200 s on Windows, and the Windows CI runner is
  # slower again.  300 s was already tight before the non-hermetic targets
  # stopped making live network calls, and it is not enough on the runner.
  if(WIN32)
    set_tests_properties(selfhost_codegen_parity PROPERTIES TIMEOUT 900)
  endif()
  add_test(NAME bindgen_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/bindgen.test.py)
  set_tests_properties(bindgen_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "compiler;regression")
  add_test(NAME wasm_backend_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/wasm_backend.test.py)
  set_tests_properties(wasm_backend_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "compiler;regression;wasm")
  # The route-A native backend, end to end: for every in-subset program,
  # `aot-native translate` plus a host `cc -O2` must reproduce the interpreter's
  # stdout byte for byte; out-of-subset programs must be REFUSED rather than
  # mistranslated.  The two pinned divergences assert both sides exactly, so
  # fixing one (or letting the gap widen) fails here and demands a decision.
  # It needs a working `cc`, which the engine build already required.
  add_test(NAME aot_native_regression
           COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/aot_native.test.py
                   --build ${CMAKE_BINARY_DIR} --cc ${CMAKE_C_COMPILER})
  set_tests_properties(aot_native_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 LABELS "compiler;aot;regression")
  add_test(NAME scan_tools_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/scan_tools.test.py)
  set_tests_properties(scan_tools_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "compiler;regression")
  add_test(NAME replay_closure_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/replay.test.py)
  set_tests_properties(replay_closure_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "runtime;regression")
  # The L_CALL_FUNC guard must fire for every out-of-range function index, not
  # only for functions whose name begins with 'h' (docs/AUDIT.md §1.3).  Before
  # the fix this probe segfaulted, so the test asserts on a signal as well.
  add_test(NAME vm_bad_fidx_regression
           COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/vm_bad_fidx.test.py $<TARGET_FILE:inimerse>)
  set_tests_properties(vm_bad_fidx_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "runtime;regression")
  set_tests_properties(cli_incremental_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "compiler;regression")
  add_test(NAME release_verify_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/release_verify.test.py)
  set_tests_properties(release_verify_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "release;regression")
  # Oversized HTTP file bodies must be refused (413), never truncated, and
  # `--port 0` / `--http-port 0` must be reported by the engine.  Needs only
  # Python and the engine binary, so it belongs to the Python-gated block.
  add_test(NAME http_large_package_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/http_large_package.test.py)
  set_tests_properties(http_large_package_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "protocol;regression")
  if(WIN32)
    # The complete hub -- /package, /package/fork, /content/, /economy/*,
    # /node/*, /session/* -- is src/platform/http_posix.c, which the source
    # lists below compile into the POSIX engine only.  The Windows engine links
    # src/mod/verse_dist_mod.c instead: a reduced hub that answers /ping,
    # /v/<id>, /hub and /api/forge, and hands every other path the 436-byte
    # "Wrong port!" page.  This regression drives the POSIX hub's body limit,
    # so on Windows it is disabled rather than failed (docs/STATUS.md §2.5-2.9).
    set_tests_properties(http_large_package_regression PROPERTIES DISABLED TRUE)
  endif()
endif()
if(INIMERSE_NODE)
  add_test(NAME crp_session_flow_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/crp_session.test.py)
  set_tests_properties(crp_session_flow_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "protocol;regression")
  add_test(NAME hub_dist_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/hub_dist.test.py)
  set_tests_properties(hub_dist_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "protocol;regression")
  add_test(NAME verse_pack_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/verse_pack.test.py)
  set_tests_properties(verse_pack_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "protocol;regression")
  # Two-way `.vverse` interoperability: the engine packer is judged by the
  # read-only JS reference (and the other way round), so both writers have to
  # be built before this can run.
  add_test(NAME vverse_cross_regression
    COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/vverse_cross.test.py $<TARGET_FILE:vverse_pack_probe>)
  set_tests_properties(vverse_cross_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "protocol;regression")
  add_test(NAME node_discovery_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/node_discovery.test.py)
  set_tests_properties(node_discovery_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "protocol;regression")
  add_test(NAME lease_handoff_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lease_handoff.test.py)
  set_tests_properties(lease_handoff_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "protocol;regression")
  add_test(NAME reconnect_generation_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/reconnect_generation.test.py)
  set_tests_properties(reconnect_generation_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "protocol;regression")
  add_test(NAME economy_domain_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/economy_domain.test.py)
  set_tests_properties(economy_domain_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "protocol;regression")
  add_test(NAME economy_migration_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/economy_migration.test.py)
  set_tests_properties(economy_migration_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "protocol;regression")
  add_test(NAME protocol_regression COMMAND ${INIMERSE_NODE} tools/regression.js)
  set_tests_properties(protocol_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 30 LABELS "protocol;regression")
  add_test(NAME verse_upp_crosscheck
    COMMAND ${INIMERSE_NODE} ${CMAKE_SOURCE_DIR}/tools/upp_engine_crosscheck.js
            $<TARGET_FILE:verse_upp_probe>)
  set_tests_properties(verse_upp_crosscheck PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60 LABELS "protocol;upp")
  add_test(NAME verse_crp_crosscheck
    COMMAND ${INIMERSE_NODE} ${CMAKE_SOURCE_DIR}/tools/crp_engine_crosscheck.js
            $<TARGET_FILE:verse_crp_probe>)
  set_tests_properties(verse_crp_crosscheck PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120 LABELS "protocol;crp")
  if(WIN32)
    # These eight drive the POSIX hub's routes -- /package, /content/, /v/<id>
    # over the WebSocket portal, /economy/*, /node/*, /session/* -- which only
    # src/platform/http_posix.c serves.  The Windows engine's reduced hub
    # (src/mod/verse_dist_mod.c) cannot answer them, so they are disabled
    # rather than failed; the same reasoning as http_large_package_regression
    # above.  `verse_pack_regression` is deliberately NOT in this list: the VDP
    # is engine-side and must pass on Windows.
    set_tests_properties(crp_session_flow_regression hub_dist_regression
                         vverse_cross_regression node_discovery_regression
                         lease_handoff_regression reconnect_generation_regression
                         economy_domain_regression economy_migration_regression
                         PROPERTIES DISABLED TRUE)
  endif()
endif()

find_program(INIMERSE_CLANG clang)
if(INIMERSE_CLANG AND NOT WIN32)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/wasm_probe.wasm
    COMMAND ${INIMERSE_CLANG} --target=wasm32-wasi -nostdlib -Wl,--no-entry
      -Wl,--export=inimerse_probe -Wl,--export=inimerse_abi_version
      -Wl,--export=inimerse_capabilities ${CMAKE_SOURCE_DIR}/tools/wasm_probe.c
      -o ${CMAKE_BINARY_DIR}/wasm_probe.wasm
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/wasm_probe.c)
  # Build the artifact as part of the default build so a plain
  # `cmake --build && ctest` cannot register a test for a missing file.
  add_custom_target(wasm_probe ALL DEPENDS ${CMAKE_BINARY_DIR}/wasm_probe.wasm)
  install(FILES ${CMAKE_BINARY_DIR}/wasm_probe.wasm
    DESTINATION share/inimerse OPTIONAL)
  if(INIMERSE_NODE)
    add_test(NAME wasm_probe COMMAND ${INIMERSE_NODE} ${CMAKE_SOURCE_DIR}/tools/wasm_probe_check.js ${CMAKE_BINARY_DIR}/wasm_probe.wasm)
    add_test(NAME wasm_host COMMAND ${INIMERSE_NODE} ${CMAKE_SOURCE_DIR}/tools/wasm_host.test.js ${CMAKE_BINARY_DIR}/wasm_probe.wasm)
    # `%` and `/` must mean the same thing on all three backends, including
    # which cases are refused (docs/AUDIT.md §1.6).  It needs node because the
    # wasm backend is the third opinion.
    add_test(NAME mod_semantics_regression
             COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/mod_semantics.test.py
                     $<TARGET_FILE:inimerse> $<TARGET_FILE:aot-native>
                     --node ${INIMERSE_NODE})
    set_tests_properties(mod_semantics_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 LABELS "runtime;compiler;wasm;regression")
    # `and`/`or`/`not` must mean the same thing on all three backends: a
    # truth-value, short-circuit preserved, in value position and in condition
    # position (docs/AUDIT.md §1.0, §5 O0).  It found an inverted `not` in the
    # wasm backend the day it was written.  The same file also pins a `+` chain
    # of three or more terms -- one OP_CONCAT, which used to wrap where the
    # two-term OP_ADD promoted (docs/AUDIT.md §1.7) -- and printed floats, which
    # each backend used to format differently.  It needs node because wasm is
    # the third opinion.
    add_test(NAME logic_semantics_regression
             COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/logic_semantics.test.py
                     $<TARGET_FILE:inimerse> $<TARGET_FILE:aot-native>
                     --node ${INIMERSE_NODE})
    set_tests_properties(logic_semantics_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 LABELS "language;compiler;wasm;regression")
  endif()
  set_tests_properties(wasm_probe wasm_host PROPERTIES TIMEOUT 10 LABELS "wasm;smoke")
endif()

# The full engine is intentionally enabled only after the VM's thread/fiber
# backend is ported.  Keeping this gate explicit prevents a misleading build
# that silently omits platform modules on POSIX systems.
if(INIMERSE_BUILD_ENGINE)
  set(INIMERSE_ENGINE_SOURCES
    src/platform/platform.c src/platform/thread.c src/platform/fiber.c
    src/platform/dir.c
    src/common/common.c src/common/sha256.c src/common/ed25519.c
    src/common/gzip.c src/common/vverse_pack.c
    src/types/typeset.c src/types/enum.c src/types/error_types.c src/types/registry.c
    src/vm/closure.c
    src/lexer/lexer.c src/parser/parser.c
    src/compiler/bytecode.c src/compiler/compiler.c
    src/compilation/checksum.c src/compilation/deps.c src/compilation/profiler.c src/compilation/debug_info.c src/compilation/wasm_backend.c
    src/vm/vm.c src/vm/params_v2.c src/vm/jit_mode.c
    # `vm_exec` is registered by BOTH runtimes (runtime.c on WIN32,
    # runtime_posix.c elsewhere), so its implementation must build on both.
    src/runtime/vm_exec_builtin.c)
  if(WIN32)
    list(APPEND INIMERSE_ENGINE_SOURCES
      src/platform/process.c src/platform/socket.c
      src/runtime/runtime.c src/child_proc.c src/headless_server.c
      src/isolate_mod.c src/desugar_mod.c src/lint_mod.c src/mod/mod.c
      src/mod/gui_mod.c src/mod/io_mod.c src/mod/net_mod.c src/mod/json_mod.c
      src/mod/record_mod.c src/mod/replay_mod.c src/mod/infiverse_mod.c src/mod/verse_dist_mod.c
      src/mod/server_mod.c src/mod/identity_mod.c src/mod/social_mod.c
      src/mod/ai_mod.c src/mod/result_mod.c mods/build/build_mod.c
      src/verse/json_min.c)
    list(APPEND INIMERSE_ENGINE_SOURCES src/mod/say_mod_windows.c src/mod/say_stream.c)
  else()
    list(APPEND INIMERSE_ENGINE_SOURCES src/runtime/runtime_posix.c
      src/mod/mod_posix.c src/mod/net_mod_posix.c src/mod/server_mod_posix.c src/mod/say_mod_posix.c src/mod/say_stream.c src/platform/socket.c
      src/platform/process.c src/isolate_mod.c
      src/platform/posix_stubs.c src/platform/http_posix.c src/verse/json_min.c src/verse/crp.c src/verse/upp.c src/platform/http_client.c src/platform/serial_posix.c src/platform/websocket.c src/platform/crp_session.c src/desugar_mod.c src/lint_mod.c
      src/headless_server_posix.c src/mod/replay_mod.c src/mod/json_mod.c src/mod/result_mod.c
      src/mod/infiverse_mod.c src/mod/record_mod.c src/mod/verse_dist_mod.c)
  endif()
  # The engine body is an OBJECT library so the cross-language bridges
  # (Python C extension, JNI shared library) can be linked against exactly the
  # same objects the `inimerse` executable runs.  src/main.c stays out of it:
  # only the CLI provides main(), so both the executable and a shared library
  # can carry the engine.  Position-independent code costs a little in the
  # executable and is what makes the same objects loadable from a .so.
  add_library(inimerse_engine OBJECT ${INIMERSE_ENGINE_SOURCES})
  set_target_properties(inimerse_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(inimerse_engine PUBLIC src src/parser src/compiler src/vm
    src/runtime src/mod src/common src/lexer src/platform src/verse)
  target_link_libraries(inimerse_engine PUBLIC inimerse_platform)
  if(NOT WIN32)
    target_compile_definitions(inimerse_engine PUBLIC
      _GNU_SOURCE _stricmp=strcasecmp
      INIMERSE_VERSION_STRING="${INIMERSE_PACKAGE_VERSION}")
  else()
    target_compile_definitions(inimerse_engine PUBLIC
      INIMERSE_VERSION_STRING="${INIMERSE_PACKAGE_VERSION}")
  endif()

  add_executable(inimerse src/main.c)
  target_link_libraries(inimerse PRIVATE inimerse_engine)
  if(WIN32)
    target_link_libraries(inimerse PRIVATE m winhttp crypt32 gdi32 winmm msimg32
      windowscodecs ole32 ws2_32 comdlg32 shell32 shlwapi iphlpapi)
  else()
    target_link_libraries(inimerse PRIVATE m Threads::Threads ${CMAKE_DL_LIBS})
  endif()
  install(TARGETS inimerse RUNTIME DESTINATION bin)

  # ------------------------------------------------------------------------
  # Route-A native backend: `aot-native translate prog.im prog.c`, then a host
  # `cc` compiles the generated C.  It links against exactly the engine objects
  # the interpreter links, minus src/main.c — the tool carries its own main().
  #
  # This target used to be produced by tools/aot_native_build.sh, which globbed
  # build/CMakeFiles/inimerse.dir/**/*.o behind CMake's back.  That stop-gap
  # required an already-configured build tree, went silently stale whenever the
  # engine gained a translation unit, and — because nothing in the gate ever
  # invoked it — left 636 lines of committed C that no test looked at.
  add_executable(aot-native
    src/compilation/aot_native.c
    src/compilation/aot_native_tool.c)
  target_link_libraries(aot-native PRIVATE inimerse_engine)
  if(WIN32)
    target_link_libraries(aot-native PRIVATE m winhttp crypt32 gdi32 winmm msimg32
      windowscodecs ole32 ws2_32 comdlg32 shell32 shlwapi iphlpapi)
  else()
    target_link_libraries(aot-native PRIVATE m Threads::Threads ${CMAKE_DL_LIBS})
  endif()

  # ------------------------------------------------------------------------
  # Cross-language bridges (v0.5 roadmap §2.1 / §3.2): one IDL, real artifacts.
  #
  # This host ships no python3-dev headers and no JDK, and the build user has
  # no sudo, so the toolchain is unpacked into a prefix outside the repository
  # (examples/BUILDING_BRIDGES.md records the exact commands).  Point
  # -DXLANG_TOOLCHAIN=/somewhere or $XLANG_TOOLCHAIN at it; the default below
  # is a per-user prefix, never a hard-coded absolute path in the tree.
  #
  # The two regressions are registered unconditionally further down; only the
  # artifacts are conditional.  A missing toolchain therefore shows up as a
  # CTest skip, never as a silently smaller test count.
  # ------------------------------------------------------------------------
  if(NOT INIMERSE_PYTHON)
    set(XLANG_TEST_PYTHON "python3")
  else()
    set(XLANG_TEST_PYTHON "${INIMERSE_PYTHON}")
  endif()
  if(DEFINED ENV{XLANG_TOOLCHAIN} AND NOT "$ENV{XLANG_TOOLCHAIN}" STREQUAL "")
    set(XLANG_TOOLCHAIN_DEFAULT "$ENV{XLANG_TOOLCHAIN}")
  else()
    set(XLANG_TOOLCHAIN_DEFAULT "$ENV{HOME}/.local/xlang-toolchain")
  endif()
  set(XLANG_TOOLCHAIN "${XLANG_TOOLCHAIN_DEFAULT}" CACHE PATH
    "Prefix holding python3.14 headers and a JDK (see examples/BUILDING_BRIDGES.md)")
  set(XLANG_PYTHON_INCLUDE "${XLANG_TOOLCHAIN}/usr/include/python3.14")
  set(XLANG_JAVA_HOME "${XLANG_TOOLCHAIN}/usr/lib/jvm/java-17-openjdk-amd64")
  set(XLANG_BRIDGE_DIR "${CMAKE_BINARY_DIR}/bridge")

  # Python.h pulls <multiarch>/python3.14/pyconfig.h, which libpython3.14-dev
  # installs one level up (usr/include/x86_64-linux-gnu/python3.14/pyconfig.h).
  execute_process(
    COMMAND "${XLANG_TEST_PYTHON}" -c
            "import sysconfig,sys;sys.stdout.write(sysconfig.get_config_var('MULTIARCH') or '')"
    OUTPUT_VARIABLE XLANG_PYTHON_MULTIARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(XLANG_PYTHON_ARCH_INCLUDE
    "${XLANG_TOOLCHAIN}/usr/include")

  set(XLANG_HAVE_TOOLCHAIN FALSE)
  if(EXISTS "${XLANG_PYTHON_INCLUDE}/Python.h"
     AND EXISTS "${XLANG_TOOLCHAIN}/usr/include/${XLANG_PYTHON_MULTIARCH}/python3.14/pyconfig.h"
     AND EXISTS "${XLANG_JAVA_HOME}/include/jni.h"
     AND EXISTS "${XLANG_JAVA_HOME}/bin/javac")
    set(XLANG_HAVE_TOOLCHAIN TRUE)
  endif()

  if(XLANG_HAVE_TOOLCHAIN)
    execute_process(
      COMMAND "${XLANG_TEST_PYTHON}" -c
              "import sysconfig,sys;sys.stdout.write(sysconfig.get_config_var('EXT_SUFFIX') or '.so')"
      OUTPUT_VARIABLE XLANG_EXT_SUFFIX OUTPUT_STRIP_TRAILING_WHITESPACE)

    # --- one IDL drives both artifacts ------------------------------------
    set(XLANG_ABI_HEADER "${CMAKE_BINARY_DIR}/bindings/c/inimerse_bridge.h")
    set(XLANG_JAVA_SRC "${CMAKE_BINARY_DIR}/bindings/java/InimerseBridge.java")
    set(XLANG_JAVA_CLASSES_DIR "${CMAKE_BINARY_DIR}/bindings/java/classes")
    set(XLANG_JAVA_STAMP "${XLANG_JAVA_CLASSES_DIR}/javac.stamp")
    set(XLANG_JAR "${XLANG_BRIDGE_DIR}/InimerseBridge.jar")
    set(XLANG_DEF "${CMAKE_SOURCE_DIR}/src/bridge/inimerse_bridge.def")

    add_custom_command(OUTPUT "${XLANG_ABI_HEADER}"
      COMMAND "${XLANG_TEST_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/bindgen.py"
              "${XLANG_DEF}" --language c --out "${XLANG_ABI_HEADER}"
      DEPENDS "${XLANG_DEF}" "${CMAKE_SOURCE_DIR}/tools/bindgen.py"
      COMMENT "bindgen: C ABI header for inimerse_bridge" VERBATIM)

    add_custom_command(OUTPUT "${XLANG_JAVA_SRC}"
      COMMAND "${XLANG_TEST_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/bindgen.py"
              "${XLANG_DEF}" --language java --out "${XLANG_JAVA_SRC}"
      DEPENDS "${XLANG_DEF}" "${CMAKE_SOURCE_DIR}/tools/bindgen.py"
      COMMENT "bindgen: InimerseBridge.java" VERBATIM)

    # The engine's translation units call inim_load_text() (see
    # src/parser/parser.c and src/compiler/compiler.c).  That definition lives in
    # src/common/common.c, which is part of the inimerse_engine object library,
    # so both bridge targets simply link the engine and neither compiles
    # src/main.c any more.  (They used to: the symbol used to be defined only in
    # src/main.c, which is deliberately not in the engine library, so each bridge
    # target compiled that whole file a second time with `main` renamed.  A
    # duplicate of the function also lived in aot_native_tool.c for the same
    # reason; both are gone.)
    # --- the native library the generated Java facade loads ---------------
    add_library(inimerse_bridge SHARED
      src/bridge/bridge_abi.c src/bridge/bridge_jni.c "${XLANG_ABI_HEADER}")
    set_target_properties(inimerse_bridge PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY "${XLANG_BRIDGE_DIR}")
    target_include_directories(inimerse_bridge PRIVATE
      "${CMAKE_BINARY_DIR}/bindings/c"
      "${XLANG_JAVA_HOME}/include" "${XLANG_JAVA_HOME}/include/linux")
    target_link_libraries(inimerse_bridge PRIVATE inimerse_engine)

    # --- the CPython extension the wheel ships ----------------------------
    add_library(inimerse_python MODULE
      src/bridge/inimerse_extension.c src/bridge/bridge_abi.c "${XLANG_ABI_HEADER}")
    set_target_properties(inimerse_python PROPERTIES
      PREFIX "" OUTPUT_NAME "inimerse" SUFFIX "${XLANG_EXT_SUFFIX}"
      LIBRARY_OUTPUT_DIRECTORY "${XLANG_BRIDGE_DIR}")
    target_include_directories(inimerse_python PRIVATE
      "${CMAKE_BINARY_DIR}/bindings/c" "${XLANG_PYTHON_INCLUDE}"
      "${XLANG_PYTHON_ARCH_INCLUDE}")
    target_link_libraries(inimerse_python PRIVATE inimerse_engine)

    # --- compile + package the Java facade --------------------------------
    add_custom_command(OUTPUT "${XLANG_JAVA_STAMP}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${XLANG_JAVA_CLASSES_DIR}"
      COMMAND "${XLANG_JAVA_HOME}/bin/javac" -d "${XLANG_JAVA_CLASSES_DIR}"
              "${XLANG_JAVA_SRC}" "${CMAKE_SOURCE_DIR}/src/bridge/InimerseBridgeMain.java"
      COMMAND "${CMAKE_COMMAND}" -E touch "${XLANG_JAVA_STAMP}"
      DEPENDS "${XLANG_JAVA_SRC}" "${CMAKE_SOURCE_DIR}/src/bridge/InimerseBridgeMain.java"
      COMMENT "javac: InimerseBridge + InimerseBridgeMain" VERBATIM)

    add_custom_command(OUTPUT "${XLANG_JAR}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${XLANG_BRIDGE_DIR}"
      COMMAND "${XLANG_JAVA_HOME}/bin/jar" cf "${XLANG_JAR}"
              -C "${XLANG_JAVA_CLASSES_DIR}" .
      DEPENDS "${XLANG_JAVA_STAMP}"
      COMMENT "jar: InimerseBridge.jar" VERBATIM)

    add_custom_target(inimerse_bridge_jar ALL DEPENDS "${XLANG_JAR}")
    add_dependencies(inimerse_bridge_jar inimerse_bridge)
  else()
    message(WARNING
      "xlang bridges: no toolchain at ${XLANG_TOOLCHAIN} "
      "(need usr/include/python3.14/Python.h and usr/lib/jvm/java-17-openjdk-amd64). "
      "The bridge artifacts are not built and the two bridge regressions will skip. "
      "See examples/BUILDING_BRIDGES.md.")
  endif()

  # Registered unconditionally: if the toolchain is absent these exit 77 and
  # CTest reports them as skipped, so the suite never looks greener than it is.
  add_test(NAME xlang_python_bridge
    COMMAND "${XLANG_TEST_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/xlang_python_bridge.test.py"
            "${CMAKE_BINARY_DIR}" "${XLANG_TOOLCHAIN}")
  set_tests_properties(xlang_python_bridge PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 300
    LABELS "bridge;python" SKIP_RETURN_CODE 77)

  add_test(NAME xlang_java_bridge
    COMMAND "${XLANG_TEST_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/xlang_java_bridge.test.py"
            "${CMAKE_BINARY_DIR}" "${XLANG_TOOLCHAIN}")
  set_tests_properties(xlang_java_bridge PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 300
    LABELS "bridge;java" SKIP_RETURN_CODE 77)

  add_test(NAME version_runtime COMMAND inimerse --version)
  set_tests_properties(version_runtime PROPERTIES
    TIMEOUT 15 LABELS "release;metadata"
    PASS_REGULAR_EXPRESSION "inimerse ${INIMERSE_PACKAGE_VERSION}")
  add_test(NAME result_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/result_v04.im)
  set_tests_properties(result_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;result" PASS_REGULAR_EXPRESSION "result is_ok=true.*result unwrap_or=42.*result is_ok_err=false.*result unwrap_or_err=99.*result unwrap=42")
  add_test(NAME collection_comprehension_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/collection_comprehension_v04.im)
  set_tests_properties(collection_comprehension_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar" PASS_REGULAR_EXPRESSION "102")
  add_test(NAME collection_core_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/collection_core_v04.im)
  set_tests_properties(collection_core_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;collection" PASS_REGULAR_EXPRESSION "1,2")
  add_test(NAME type_collection_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/type_collection_v04.im)
  set_tests_properties(type_collection_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;types;collection" PASS_REGULAR_EXPRESSION "typecoll x=42")
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/range_meta_sandbox)
  add_test(NAME range_meta_runtime
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/range_meta_sandbox
      $<TARGET_FILE:inimerse> --no-mods ${CMAKE_SOURCE_DIR}/vtest/range_meta_v04.im)
  set_tests_properties(range_meta_runtime PROPERTIES TIMEOUT 15 LABELS "language;types;metadata" PASS_REGULAR_EXPRESSION "range-meta-pass")
  add_test(NAME posix_core_api_runtime COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/posix_core_api_v04.im)
  set_tests_properties(posix_core_api_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;collection;strings" PASS_REGULAR_EXPRESSION "posix-core-api-ok")
  add_test(NAME posix_runtime_parity COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/posix_runtime_parity_v04.im)
  # Enabled on both platforms.  It used to be `DISABLED TRUE` on Windows, which left the
  # only test that compares the two runtimes unrun on the platform where they diverge --
  # and a disabled test reports as "passed" (docs/AUDIT.md §1.37, §1.43).  The one known
  # divergence is asserted per platform instead: `load_params` on a missing file answers
  # -1 on POSIX (posix_unsupported) and 0 on WIN32 (a real implementation), so each side's
  # CURRENT value is pinned and either side drifting turns this red.  Note that the old
  # `DISABLED TRUE` branch set no assertions at all, so deleting it alone would have
  # produced a green test that asserted nothing.
  if(WIN32)
    set(INIMERSE_PARITY_LOAD_MISSING "parity load_missing=0")
  else()
    set(INIMERSE_PARITY_LOAD_MISSING "parity load_missing=-1")
  endif()
  set_tests_properties(posix_runtime_parity PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;posix" PASS_REGULAR_EXPRESSION "posix-runtime-parity-ok;${INIMERSE_PARITY_LOAD_MISSING}" FAIL_REGULAR_EXPRESSION "posix-runtime-parity-fail")
  add_test(NAME gc_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/gc_runtime_v04.im)
  set_tests_properties(gc_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;memory;gc" PASS_REGULAR_EXPRESSION "gc-ran;gc-stats-ok;thread-result-alive;closure-capture-alive;root-alive;gc-done")
  add_test(NAME pipeline_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/pipeline_v04.im)
  set_tests_properties(pipeline_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar" PASS_REGULAR_EXPRESSION "pipe one=5.*pipe two=4.*pipe arg=5.*pipe order=1")
  add_test(NAME result_question_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/result_question_v04.im)
  # Assert on the marked VALUE line, not on the unreachable literal.  This was forced by a
  # compiler debug dump that wrote the first 16 string-pool entries to stderr on every
  # compile (`src/compiler/compiler.c:3116`, removed this round), which made
  # FAIL_REGULAR_EXPRESSION "resultq unreachable" match the echo even when the statement
  # never ran.  The dump is gone; asserting on values is still the rule (docs/SYNTAX.md
  # §7.2 M13: 66 of 101 CTest cases asserted only the exit code).
  set_tests_properties(result_question_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;result" PASS_REGULAR_EXPRESSION "resultq value=42.*resultq caught.*resultq reached=0")
  add_test(NAME null_coalesce_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/null_coalesce_v04.im)
  set_tests_properties(null_coalesce_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;null-safety" PASS_REGULAR_EXPRESSION "coalesce nil=42.*coalesce some=7")
  add_test(NAME optional_member_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/optional_member_v04.im)
  # The old compiler string-pool dump echoed the dict literal's own "inimerse" even when
  # `?.` was broken (that dump is removed; see the note on result_question_runtime above)
  # -- assert on the OM-prefixed value lines. PASS catches the key being truncated
  # (bytecode_add_string got a non-NUL-terminated StringView); FAIL catches the
  # stale recycled register that made nil?.name return the previous result.
  set_tests_properties(optional_member_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;null-safety" PASS_REGULAR_EXPRESSION "OM present=inimerse" FAIL_REGULAR_EXPRESSION "OM missing=inimerse")
  add_test(NAME chained_comparison_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/chained_comparison_v04.im)
  set_tests_properties(chained_comparison_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;comparison" PASS_REGULAR_EXPRESSION "chain inside-hit.*chain hits=1 end")
  add_test(NAME postfix_condition_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/postfix_condition_v04.im)
  set_tests_properties(postfix_condition_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;control-flow" PASS_REGULAR_EXPRESSION "postfix if-true.*postfix say-if.*postfix unless-block.*postfix fired=0")
  # Regression for the cross-line postfix-if ambiguity: a next-line `if cond { ... }` used to be
  # swallowed as a postfix condition of the previous statement, and the orphaned block was then
  # parsed as a dict literal (`expected ':', but got '}'`).  The test must both run (exit 0 is the
  # primary assertion) and keep the same-line postfix working while unless still suppresses.
  add_test(NAME postfix_condition_multiline_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/postfix_condition_multiline_v05.im)
  set_tests_properties(postfix_condition_multiline_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;control-flow" PASS_REGULAR_EXPRESSION "postfix-ok.*multiline-if-ok" FAIL_REGULAR_EXPRESSION "not-printed")
  add_test(NAME result_propagation_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/result_propagation_v04.im)
  set_tests_properties(result_propagation_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;result" PASS_REGULAR_EXPRESSION "resultprop unwrapped=7.*resultprop err_is_ok=false.*resultprop err=inner")
  add_test(NAME result_finally_propagation_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/result_finally_propagation_probe_v04.im)
  set_tests_properties(result_finally_propagation_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;error-handling;result" PASS_REGULAR_EXPRESSION "finally-ran" FAIL_REGULAR_EXPRESSION "inner")
  add_test(NAME try_finally_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/try_finally_v04.im)
  set_tests_properties(try_finally_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;error-handling" PASS_REGULAR_EXPRESSION "finally normal=101.*finally nested=100.*finally nested err=rethrow-me.*finally caught=1110")
  add_test(NAME finally_return_override_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/finally_return_override_v04.im)
  set_tests_properties(finally_return_override_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;error-handling;control-flow" PASS_REGULAR_EXPRESSION "8.*1.*12.*111")
  add_test(NAME finally_break_diagnostic COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/finally_control_flow.test.py $<TARGET_FILE:inimerse> ${CMAKE_SOURCE_DIR}/vtest/finally_control_flow_probe_v04.im "break is not allowed in finally")
  set_tests_properties(finally_break_diagnostic PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;error-handling;control-flow")
  add_test(NAME finally_continue_diagnostic COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/finally_control_flow.test.py $<TARGET_FILE:inimerse> ${CMAKE_SOURCE_DIR}/vtest/finally_continue_control_flow_probe_v04.im "continue is not allowed in finally")
  set_tests_properties(finally_continue_diagnostic PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;error-handling;control-flow")
  add_test(NAME finally_goto_diagnostic COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/finally_control_flow.test.py $<TARGET_FILE:inimerse> ${CMAKE_SOURCE_DIR}/vtest/finally_goto_control_flow_probe_v04.im "goto is not allowed in finally")
  set_tests_properties(finally_goto_diagnostic PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;error-handling;control-flow")
  add_test(NAME case_try_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_try_v04.im)
  # `.im` -> real `.vverse`: drive the engine builtin `verse_pack` (src/mod/
  # verse_dist_mod.c) through a script and judge the artifact with the read-only
  # reference (tools/vverse_pack.js, tools/vverse_validate.js) plus the engine's
  # own reader.  The test also proves packing leaves the source tree untouched
  # and is byte-deterministic.  Node is the judge, so gate it on INIMERSE_NODE.
  if(INIMERSE_NODE)
    add_test(NAME vverse_cli_regression
      COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/vverse_cli.test.py
              $<TARGET_FILE:inimerse> $<TARGET_FILE:vverse_pack_probe>)
    set_tests_properties(vverse_cli_regression PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "protocol;regression")
  endif()
  set_tests_properties(case_try_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;result;case" PASS_REGULAR_EXPRESSION "deep-structural-hit;nested-result-hit")
  add_test(NAME lint_case_try_runtime COMMAND inimerse --lint ${CMAKE_SOURCE_DIR}/vtest/lint_case_try_v04.im)
  set_tests_properties(lint_case_try_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;lint;case;result" PASS_REGULAR_EXPRESSION "guarded err coverage" FAIL_REGULAR_EXPRESSION "line 11")
  add_test(NAME lint_case_enum_runtime COMMAND inimerse --lint ${CMAKE_SOURCE_DIR}/vtest/lint_case_enum_v04.im)
  set_tests_properties(lint_case_enum_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;lint;case;types" PASS_REGULAR_EXPRESSION "missing members: E, W" FAIL_REGULAR_EXPRESSION "line 11")
  add_test(NAME lint_case_membership_runtime COMMAND inimerse --lint ${CMAKE_SOURCE_DIR}/vtest/lint_case_membership_v04.im)
  set_tests_properties(lint_case_membership_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;lint;case;types" PASS_REGULAR_EXPRESSION "finite-set membership branch already covers" FAIL_REGULAR_EXPRESSION "line 4")
  add_test(NAME lint_case_try_members_runtime COMMAND inimerse --lint ${CMAKE_SOURCE_DIR}/vtest/lint_case_try_members_v04.im)
  set_tests_properties(lint_case_try_members_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;lint;case;result" PASS_REGULAR_EXPRESSION "finite err type 'FileError' is missing members: permission_denied, disk_full" FAIL_REGULAR_EXPRESSION "line 11")
  add_test(NAME lint_case_try_alias_runtime COMMAND inimerse --lint ${CMAKE_SOURCE_DIR}/vtest/lint_case_try_alias_v04.im)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/lint_alias_sandbox)
  set_tests_properties(lint_case_try_alias_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/lint_alias_sandbox TIMEOUT 15 LABELS "language;lint;case;result;types" FAIL_REGULAR_EXPRESSION "case try guarded err coverage|finite err type")
  # The five lint_case_* tests above assert their findings through CTest's
  # PASS_REGULAR_EXPRESSION, and that property takes precedence over the process
  # exit code -- so none of them can see whether --lint's own verdict is
  # readable by a shell, and none can notice an *extra* finding.  This driver
  # asserts the whole table at once: exit code (1 = findings, 2 = unreadable,
  # 0 = clean), exact finding count and required text, plus one pinned
  # limitation (a syntax error stays invisible to the line-based scanner).
  # See docs/SYNTAX.md M11.
  add_test(NAME lint_expected_regression
    COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lint_expected.test.py $<TARGET_FILE:inimerse>)
  set_tests_properties(lint_expected_regression PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "language;lint;regression")
  add_test(NAME case_collection_patterns_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_collection_patterns_v04.im)
  # The `=NN` values come from `+ str(N)` in the script, so this regex can only
  # be satisfied by the program's own output -- never by the engine's echo line,
  # which lists every string literal of the program and would match a marker
  # built from literals alone.
  set_tests_properties(case_collection_patterns_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;collection;types" PASS_REGULAR_EXPRESSION "coll positive-type=42.*coll range-hit=7.*coll wildcard-hit=999")
  add_test(NAME case_structural_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_structural_v04.im)
  set_tests_properties(case_structural_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;structure" PASS_REGULAR_EXPRESSION "struct record-hit=1.*struct nested-hit=2.*struct deep-hit=3.*struct bind=not_found:1.*struct missing-safe=5")
  add_test(NAME case_alias_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_alias_v04.im)
  # The `alias ` prefixes make the assertion target the values the program
  # prints.  A bare `42` would also match the engine's echo of the string
  # literals, so it would pass even with the alias stuck at nil.
  set_tests_properties(case_alias_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;structure" PASS_REGULAR_EXPRESSION "alias whole=42.*alias ranged=42.*alias guarded=42.*alias guarded2-miss")
  add_test(NAME case_array_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_array_v04.im)
  set_tests_properties(case_array_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;structure" PASS_REGULAR_EXPRESSION "30.*20")
  add_test(NAME case_nested_patterns_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_nested_patterns_v04.im)
  set_tests_properties(case_nested_patterns_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;structure" PASS_REGULAR_EXPRESSION "53;nested-fallback-ok;nested-literal-ok")
  add_test(NAME lambda_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/lambda_v04.im)
  add_test(NAME composition_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/composition_v04.im)
  set_tests_properties(composition_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional" PASS_REGULAR_EXPRESSION "8")
  add_test(NAME lambda_capture_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/lambda_capture_v04.im)
  # Renamed from lambda_capture_rejected_v04.im: bd69935 implemented captured
  # lambdas and flipped the file's expectation to the working case without
  # renaming it, so the name claimed a contract that no longer existed.
  set_tests_properties(lambda_capture_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional" PASS_REGULAR_EXPRESSION "lambda capture2=5.*lambda capture10=13.*lambda local=8")
  add_test(NAME lambda_nested_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/lambda_nested_v04.im)
  add_test(NAME function_value_lifetime_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/function_value_lifetime_v04.im)
  set_tests_properties(function_value_lifetime_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;functional;memory" PASS_REGULAR_EXPRESSION "12.*7.*closure-throw-caught")
  add_test(NAME function_thread_lifetime_runtime COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/function_thread_lifetime_v04.im)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/function_thread_sandbox)
  set_tests_properties(function_thread_lifetime_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/function_thread_sandbox TIMEOUT 15 LABELS "language;functional;memory;thread" PASS_REGULAR_EXPRESSION "9.*function-thread-lifetime-ok")
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/thread_runtime_sandbox)
  add_test(NAME thread_result_runtime COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/thread_result_v04.im)
  set_tests_properties(thread_result_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/thread_runtime_sandbox TIMEOUT 15 LABELS "language;thread;result;error-handling" PASS_REGULAR_EXPRESSION "worker-failed;release-plain;released-result-is-err;restarted-plain;release-task;restarted-task;release-running;release-finished")
  add_test(NAME thread_await_runtime COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/thread_await_v04.im)
  set_tests_properties(thread_await_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/thread_runtime_sandbox TIMEOUT 30 LABELS "language;thread;result;error-handling" PASS_REGULAR_EXPRESSION "thread-await-ok" FAIL_REGULAR_EXPRESSION "thread-await-fail!")
  add_test(NAME eidos_desugar_runtime COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/eidos_desugar.test.py)
  set_tests_properties(eidos_desugar_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;eidos")
  add_test(NAME eidos_runtime COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/eidos_runtime.test.py $<TARGET_FILE:inimerse>)
  set_tests_properties(eidos_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;eidos")
  set_tests_properties(lambda_nested_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional" PASS_REGULAR_EXPRESSION "lambda nested=9")
  set_tests_properties(lambda_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional" PASS_REGULAR_EXPRESSION "lambda double=42.*lambda sum2=5")
  add_test(NAME float_precision_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/float_precision_v04.im)
  set_tests_properties(float_precision_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;numeric" PASS_REGULAR_EXPRESSION "float value=1.2345678901234567")
  # Regression for BOARD row `engine-push-call-arg-miscompile`: a user-function call
  # nested inside another call's argument list corrupted the VM operand stack because
  # L_RETURN restored `t->sp` from frame_sp[frame_count] instead of [frame_count - 1].
  # Both forms exit 0 whether or not the defect is present, so these tests assert
  # NUMBERS, never exit codes.  The engine prints module-load preamble lines before the
  # program output, so the patterns are substrings, not whole-output anchors.
  # `alone` is the control (it produced the right answer even on main); `shared` is the
  # one that fails on main, and the pair is what pins "both forms must agree".
  add_test(NAME call_arg_clobber_alone_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/call_arg_clobber_alone_v05.im)
  set_tests_properties(call_arg_clobber_alone_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;compiler;regression" PASS_REGULAR_EXPRESSION "callarg-alone-ok n=2" FAIL_REGULAR_EXPRESSION "n=1|n=0")
  add_test(NAME call_arg_clobber_shared_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/call_arg_clobber_shared_v05.im)
  set_tests_properties(call_arg_clobber_shared_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;compiler;regression" PASS_REGULAR_EXPRESSION "callarg-shared-ok m1=1 m2=2 m3=2 m4=1 m5=1 dk=1 nest=1 rep=2,2,2" FAIL_REGULAR_EXPRESSION "m2=1|dk=0|nest=0|rep=1,1,1")
  # Regression for the `list`/`len` off-by-one on sets (docs/AUDIT.md §1.9):
  # vm_set_to_array() handed back an already-1-based array handle while all three of
  # its callers treat the result as a raw pool index, so `list(<set>)` answered the
  # NEXT pool slot -- an unrelated, usually empty array -- and `len(<set>)` measured
  # that same wrong array.  The defect changes VALUES, never the exit code, so this
  # asserts numbers.  The "a set in a variable is not enumerable at all" gap that
  # this line used to name as a separate defect is now fixed and pinned by
  # set_components_enumerable_runtime below.
  add_test(NAME list_set_off_by_one_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/list_set_off_by_one_v05.im)
  set_tests_properties(list_set_off_by_one_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "listset-ok n=3 first=1 last=3 setlen=3" FAIL_REGULAR_EXPRESSION "n=0|first=nil|setlen=0")
  # A set literal is a union of i64, items and interval COMPONENTS; enumerating only
  # the first two made list()/len()/size() blind to any set written as `1, 2, Z[7~9]`
  # or `z = Z[1~3]`, and vm_set_add_comp dropped the i64 half of a nested literal.
  # Values only, never exit codes.  See docs/AUDIT.md §1.10.
  add_test(NAME set_components_enumerable_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/set_components_enumerable_v05.im)
  set_tests_properties(set_components_enumerable_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "setcomp-ok union=5/1,9 two=6/5 overlap=3/3 single=3 nested=4/4 size=3 infinite=0,nil" FAIL_REGULAR_EXPRESSION "union=0|overlap=0|single=0|nested=1|size=nil")
  # A FloatN/floatN interval is a lattice, so it must be walked by index: adding the
  # step drifted above the last member and hid it from list()/len() while `in` kept
  # answering true.  See docs/AUDIT.md §1.10.
  add_test(NAME set_float_lattice_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/set_float_lattice_v05.im)
  set_tests_properties(set_float_lattice_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "setlattice-ok n=4 last03=true in03=true upper=3 upper0=false half=6 int=6 big=100" FAIL_REGULAR_EXPRESSION "n=3|last03=false|in03=false|upper0=true")
  # L_CALL_BUILTIN used to fall through when the name was not registered, so the call
  # returned the last pushed operand instead of failing: str2int("42") handed back the
  # string "42" and the program kept going.  Both compilation backends already refuse
  # an unknown callee at compile time; this is the interpreter catching up.  The name
  # in the test exists in no build, so it does not depend on which modules are linked.
  # The regexes deliberately quote only text that is NOT a string literal in the test:
  # the engine dumps its string pool on stderr, and an unanchored FAIL regex that names
  # a literal in the source matches that dump and reports a false failure.
  # See docs/AUDIT.md §1.11.
  add_test(NAME unknown_builtin_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/unknown_builtin_v05.im)
  set_tests_properties(unknown_builtin_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "unreg-ok state=1 control=true" FAIL_REGULAR_EXPRESSION "unreg-ok state=0|unreg-ok state=2|control=false")

  # contract_test.im is the repo-root API/SPI conformance suite.  It had been a manual,
  # dormant suite: the OP_CONCAT integer-overflow defect (§1.8) and the list(<set>)
  # handle defect (§1.10) were both first caught by it, yet it was registered nowhere --
  # and a suite that is not run is a suite that does not exist.  It is registered here.
  # io_mod/gui_mod register str2int/noise2d/gui_*, and the POSIX build stubs those
  # modules out, so calling them now throws "unknown builtin function" (§1.11).  The
  # suite probes for each module and skips the dependent sections instead of failing,
  # prints the probe values, and asserts a floor of 60 of its 73 checks so a mass skip
  # cannot pass silently.  There is deliberately no FAIL_REGULAR_EXPRESSION: the throw
  # text is a string literal in the suite, and CTest reads the engine's stderr string
  # pool dump, so naming it would match the dump on a green run.  A throw exits 1 and
  # fails the test on the exit code alone.
  add_test(NAME contract_suite_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/contract_test.im)
  set_tests_properties(contract_suite_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "vm;language;regression;contract" PASS_REGULAR_EXPRESSION "contract: [0-9]+ passed")

  # vm_set_to_array stops at two gates and returns -1 rather than enumerating: the
  # span gate (a lattice of ten million members or more) and the range gate
  # (|endpoint| > 9.0e18, or an integer lattice outside int32, since VAL_INT holds
  # 32 bits).  Refusing is the contract -- truncating to the first N members would
  # be cheap and wrong -- but nothing pinned either gate.  len() returns 0 for a
  # refused set AND for an empty one, so the test asserts list(), where a refusal
  # is nil and an empty interval is [].
  # See docs/AUDIT.md §1.10.
  add_test(NAME set_enumeration_refusal_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/set_enumeration_refusal_v05.im)
  set_tests_properties(set_enumeration_refusal_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "setrefuse-ok bigspan=nil edge=nil i32=nil neg32=nil empty=0 ok10=10 ok3=3" FAIL_REGULAR_EXPRESSION "ok10=0|ok3=0|empty=nil")
  # sum() read a set straight out of its storage and only when it had no interval
  # COMPONENTS, so `1, 2, Z[7~9]` summed to 0 while len() was 5; and it accumulated
  # integers in a double, so anything above 2^53 was rounded before push_int saw it.
  # Both copies of the function (posix_core_sum, and builtin_sum on WIN32) are fixed.
  # A third defect lived only in POSIX: a non-numeric element was answered with nil
  # while WIN32 raised `sum: non-numeric element`, so the same program meant two
  # different things per platform -- and nil was unobservable as a failure, because
  # `sum([1, "a"]) + 1` was 1 (arithmetic absorbs nil).  Both now raise, with one
  # element rule (VAL_INT and VAL_FLOAT only).  Values only, never exit codes: the
  # raising branches are wrapped in try/catch and asserted on the message text.
  # See docs/AUDIT.md §1.15 and §1.25.
  add_test(NAME sum_components_int64_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/sum_components_int64_v06.im)
  set_tests_properties(sum_components_int64_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "sum-ok comp=27 two=14 single=10 big=9007199254740993 big2=9007199254740994 plain=6 float=3.5 empty=0 inf=nil refuse-bool=sum: non-numeric element refuse-str=sum: non-numeric element refuse-nil=sum: non-numeric element" FAIL_REGULAR_EXPRESSION "comp=0|two=0|single=0|big=9007199254740992|refuse-bool=nil|refuse-str=nil|refuse-nil=nil")
  # str() summarised a set as set(iCount + count) -- the LITERAL part only -- so
  # `str(Z[1~4])` printed set(0) beside `len(Z[1~4]) == 4`.  It now enumerates
  # through vm_set_to_array whenever the set has a component, exactly as len(),
  # size(), list() and sum() do.  Values only, never exit codes.  See §1.17.
  add_test(NAME set_str_component_count_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/set_str_component_count_v06.im)
  set_tests_properties(set_str_component_count_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "setstr-ok union=set\\(5\\)/5 single=set\\(4\\)/4 overlap=set\\(3\\)/3 two=set\\(6\\)/6 plain=set\\(3\\)/3 refused=set\\(0\\)/0 named=set\\(Z\\) interval=set\\(float1 interval\\)" FAIL_REGULAR_EXPRESSION "union=set\\(2\\)|single=set\\(0\\)|overlap=set\\(1\\)|two=set\\(0\\)|plain=set\\(0\\)")
  # Five places each decided truthiness for themselves and disagreed.  `if ""`,
  # `"" or f()`, `"" and f()`, `not ""` and `bool("")` did not share a rule: the
  # empty string was truthy for `if` but falsy for `or`/`not`/`bool`, and a dict or
  # a set did not short-circuit `or` while an array did.  They now all call one
  # helper, vm_truthy().  The pin observes the rule AND the short-circuit, because
  # the value alone does not discriminate -- `or` yields a boolean either way.  The
  # counter is an array slot on purpose: `n = n + 1` inside a function creates a
  # local that shadows the global, so the counter reads 0 no matter what ran.
  # Values only, never exit codes.  See docs/AUDIT.md §1.18.
  add_test(NAME truthiness_single_point_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/truthiness_single_point_v06.im)
  set_tests_properties(truthiness_single_point_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "truth estr:T:false:true:0:1 str:T:false:true:0:1 arr:T:false:true:0:1 dict:T:false:true:0:1 set:T:false:true:0:1 zero:F:true:false:1:0 one:T:false:true:0:1 fzero:F:true:false:1:0 nil:F:true:false:1:0" FAIL_REGULAR_EXPRESSION "estr:T:false:false:1:1|str:T:false:true:1:1|dict:T:false:true:1:1|set:T:false:true:1:1")

  # `X.type == VAL_INT ? X.ival : (int)X.fval` read the union's DOUBLE member for
  # a bool: sqrt(true) answered 2.22e-162, float(true) the bit pattern of 1, and
  # gc_auto(true) turned GC OFF.  The FAIL regex was checked both ways with
  # grep -E (matches pre-fix, not post-fix).  See docs/AUDIT.md §1.20.
  add_test(NAME union_member_tag_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/union_member_tag_v06.im)
  set_tests_properties(union_member_tag_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "union-ok sqrt-true=1 sqrt-false=0 sqrt-int=2 float-true=1 float-false=0 float-nil=0 aadd=1 aset=1 gcauto-true=1 gcauto-false=0" FAIL_REGULAR_EXPRESSION "sqrt-true=2\\.22|float-true=4\\.94|aadd=0|aset=0|gcauto-true=0")

  # `+` concatenates when EITHER operand is a string, but L_ADD sent only the
  # right operand through value_to_string() and read the left one out of
  # `a->sval` -- which is the empty string for a freshly loaded integer, so the
  # left operand vanished (`5 + "7"` answered "7").  L_CONCAT had the identical
  # asymmetry for 3+-term chains.  The FAIL regex was checked both ways with
  # grep -E (matches pre-fix, not post-fix).  See docs/AUDIT.md §1.23.
  add_test(NAME concat_left_operand_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/concat_left_operand_v06.im)
  set_tests_properties(concat_left_operand_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "concat-ok intstr=57 strlit=5a nil=nilx bool=truex float=5\\.5x chain3=578 chainix=575 rev=a5" FAIL_REGULAR_EXPRESSION "concat-ok intstr=7 |strlit=a |nil=x |bool=x |float=x |chain3=78 |chainix=75 ")
  # atomic_add/atomic_set narrowed through `int` on a 64-bit slot, so
  # atomic_set("k", 3000000000) answered -1294967296 -- silently, exit code 0.
  # The add is now a CAS loop that raises numeric_overflow instead of wrapping,
  # matching `+`.  The FAIL regex was checked both ways with grep -E (matches
  # pre-fix, not post-fix).  See docs/AUDIT.md §1.24.
  add_test(NAME atomic_int64_width_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/atomic_int64_width_v06.im)
  set_tests_properties(atomic_int64_width_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;runtime;regression" PASS_REGULAR_EXPRESSION "atomic-ok set=3000000000 add=3000000000 get=3000000000 add2p31=2147483648 get2=2147483648 small=7 getsmall=7 ovf1=numeric_overflow keep1=9223372036854775807 half=9223372036854775807 ovf2=numeric_overflow keep2=9223372036854775807" FAIL_REGULAR_EXPRESSION "set=-1294967296|add=-1294967296|get=-1294967296|add2p31=-2147483648|get2=-2147483648|ovf1=0 |half=-1 |ovf2=0 ")

  # val_cmp() fell through to val_as_double(), whose `default` is 0.0, so every
  # non-numeric value compared as the number 0: `"abc" < 1`, `[1, 2] < 1` and
  # `nil <= 0` were all true while `==` said those pairs were not equal, and
  # max() over a set holding nil answered 5.  Ordering is now defined only
  # between two strings or between two numbers; anything else raises
  # type_mismatch.  The FAIL regex was checked both ways with grep -E (matches
  # pre-fix, not post-fix).  See docs/AUDIT.md 1.26.
  add_test(NAME order_requires_orderable_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/order_requires_orderable_v06.im)
  set_tests_properties(order_requires_orderable_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "vm;language;regression" PASS_REGULAR_EXPRESSION "order-ok strnum=type_mismatch arrnum=type_mismatch nille=type_mismatch zerole=type_mismatch nillt=type_mismatch streq=false strlt=true strgt=false numlt=true numle=true boollt=true biglt=true minset=nil maxset=nil minscalar=nil" FAIL_REGULAR_EXPRESSION "strnum=true|arrnum=true|nille=true|zerole=true|nillt=true|maxset=5 ")

  # Pins "vm_init leaves no field indeterminate" (docs/AUDIT.md §1.28): prefills
  # a VM with 0xAA, calls vm_init, and checks the fields the old field-by-field
  # list never mentioned. It links the engine objects directly, which is the only
  # way a C test can observe the C-side initialization contract at all.
  add_executable(vm_init_probe src/vm/vm_init_probe.c)
  target_link_libraries(vm_init_probe PRIVATE inimerse_engine)
  if(WIN32)
    target_link_libraries(vm_init_probe PRIVATE m winhttp crypt32 gdi32 winmm msimg32
      windowscodecs ole32 ws2_32 comdlg32 shell32 shlwapi iphlpapi)
  else()
    target_link_libraries(vm_init_probe PRIVATE m Threads::Threads ${CMAKE_DL_LIBS})
  endif()
  add_test(NAME vm_init_probe COMMAND vm_init_probe)

  # Pins "a predicate answers a bool, on every platform" (docs/AUDIT.md §1.34).
  # str() is where the divergence showed: the Windows runtime built
  # startswith/endswith with push_int, so str(startswith(s," ")) answered "1"
  # there and "true" on POSIX.  Registered last so no existing #N moves.
  add_test(NAME predicate_result_type_runtime COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/predicate_result_type_v06.im)
  set_tests_properties(predicate_result_type_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;strings" PASS_REGULAR_EXPRESSION "predbool true true false 14" FAIL_REGULAR_EXPRESSION "predbool 1|predbool true 1|predbool true true 1")

  install(PROGRAMS tools/inim.py DESTINATION bin RENAME inim)
  install(FILES README.md LICENSE DESTINATION share/doc/inimerse)
else()
  message(STATUS "Building portable platform probes only; platform-specific VM modules remain disabled")
endif()

# The Python harnesses print UTF-8 (selfhost_bench.py's suite labels are Chinese),
# while a Windows console defaults to the ANSI codepage -- cp1252 on the hosted
# runners -- where print() raises UnicodeEncodeError.  Pin the stream encoding for
# every registered test, in one place, instead of inside each harness.
get_property(INIMERSE_ALL_TESTS DIRECTORY PROPERTY TESTS)
if(INIMERSE_ALL_TESTS)
  set_tests_properties(${INIMERSE_ALL_TESTS} PROPERTIES ENVIRONMENT "PYTHONIOENCODING=utf-8")
endif()

# Pins "a connect timeout covers name resolution too" (docs/AUDIT.md §1.40).
# getaddrinfo() has no timeout in any standard, and it is the step that stalls
# when DNS is unreachable, so the pin injects the stall through LD_PRELOAD
# instead of needing a broken resolver (and it never needs the network).
# POSIX-only because the injection mechanism is the dynamic loader's.
#
# This block sits after the ENVIRONMENT loop above on purpose: that loop sets
# ENVIRONMENT on every registered test, which would overwrite the LD_PRELOAD
# this test needs.  It is registered here rather than earlier so that no existing
# #N moves; every block appended after it follows the same rule, so #123 stays
# resolve_timeout_runtime (the numbering rule is written down in docs/STATUS.md
# 10.67).  The blocks below register the pinned builtin contracts first
# (#124-#134) and then three evidence cases (#135-#137) on POSIX.
if(NOT WIN32 AND INIMERSE_BUILD_ENGINE)
  add_library(slowdns_preload MODULE src/platform/slowdns_preload.c)
  target_link_libraries(slowdns_preload PRIVATE ${CMAKE_DL_LIBS})
  add_executable(resolve_timeout_probe src/platform/socket.c src/platform/thread.c src/platform/resolve_timeout_probe.c)
  target_link_libraries(resolve_timeout_probe PRIVATE Threads::Threads)
  add_test(NAME resolve_timeout_runtime COMMAND resolve_timeout_probe)
  set_tests_properties(resolve_timeout_runtime PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "platform;smoke"
    ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:slowdns_preload>"
    PASS_REGULAR_EXPRESSION "resolve_bound: ok")
endif()

# Pins "a VFS path may never escape its mount prefix" (docs/AUDIT.md §1.43).
# im_vfs_normalize()'s ".." branch searched the caller's buffer with
# strchr(out, '/') before out[w] had been terminated, so it read uninitialized
# bytes and answered differently run to run -- on this tree
# "os:/../escape" normalized successfully, i.e. the escape was allowed.  The
# probe already asserted the opposite, but nothing registered it, so the gate
# never ran it and the failure was invisible.  It reads README.md through the
# "." mount, hence the source root as its working directory.
# Registered last so no existing #N moves.
add_test(NAME vfs_probe COMMAND vfs_probe)
set_tests_properties(vfs_probe PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "platform;smoke")

# A builtin name is one contract.  src/runtime/runtime.c (WIN32) and
# src/runtime/runtime_posix.c (POSIX) register the same names through different
# code, so a name with two implementations is a name with two answers -- and the
# only test that compared them, posix_runtime_parity, is DISABLED on Windows
# (:662).  This pin runs on BOTH platforms and prints one line either way, so on
# Linux it judges the POSIX copy and on Windows the WIN32 one.  It pins
# int()'s width, chr()'s type check, and the name guard that atomic_get/atomic_set
# were missing (which crashed the Windows engine).  int("0x10") is asserted: it
# was an open divergence (0 on POSIX, 16 on WIN32) and a human ruled it to 16 on
# both platforms, so it is a resolved contract now, not a crowned side.  See
# docs/AUDIT.md 1.45.
# Registered last so no existing #N moves.
add_test(NAME divergent_builtin_contract_runtime
         COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/divergent_builtin_contract_v06.im)
set_tests_properties(divergent_builtin_contract_runtime PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;types;regression"
  PASS_REGULAR_EXPRESSION "divergent-ok i1=9007199254740993 i2=9223372036854775807 i3=16 i4=16 c1=A c2empty=true a1=type_mismatch a2=type_mismatch a3=type_mismatch a4=0"
  FAIL_REGULAR_EXPRESSION "a1=NO-THROW|a2=NO-THROW|a3=NO-THROW|a4=type_mismatch")

# ---------------------------------------------------------------------------
# round() with a non-number argument has two answers, and this pin asserts each
# platform's OWN answer instead of picking a winner: POSIX (posix_core_round,
# src/runtime/runtime_posix.c:104) returns nil, WIN32 (builtin_round,
# src/runtime/runtime.c:41) throws "round: expected number".
#
# REGISTERED, NOT RESOLVED.  Precedent: docs/AUDIT.md 1.25 -- sum() with a
# non-numeric element is nil on POSIX and a throw on WIN32, and that ruling kept
# both styles and recorded the divergence rather than unifying it.  Class:
# docs/SYNTAX.md 7.1 D5 -- a bad argument answered with nil is a silent wrong
# answer, the throw is loud; by 7's own classification the loud one is better,
# but "better" is not "decided".  See docs/AUDIT.md 1.45.
#
# Registered last so no existing #N moves.
add_test(NAME round_nonnumber_contract_runtime
         COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/round_nonnumber_contract_v06.im)
set_tests_properties(round_nonnumber_contract_runtime PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;types;regression")
if(WIN32)
  set_tests_properties(round_nonnumber_contract_runtime PROPERTIES
    PASS_REGULAR_EXPRESSION "round: expected number")
else()
  set_tests_properties(round_nonnumber_contract_runtime PROPERTIES
    PASS_REGULAR_EXPRESSION "round-nonnumber=nil")
endif()

# ---------------------------------------------------------------------------
# `.` and `?.` are two production points for one syntax, and the node carrying
# the difference is allocated by the parser.  `struct Expr`'s `member.safe`
# (src/parser/ast.h:63) was left uninitialised on the `.` path -- the parser
# allocated that node with malloc and wrote only type/object/member -- so `a.b`
# compiled as an index get whenever the allocator handed back a non-zero byte.
# A dotted global (for instance a parameter named `player.max_hp`) then silently
# read nil, and the symptom looked like a file-size threshold because the byte
# depended on the allocator's history.  Every AST node is allocated zeroed now,
# so `safe` is false by construction on `.` and explicitly true on `?.`.
#
# The probe poisons the small-object heap with 0x01 before each parse, so a node
# that is not zero-initialised is guaranteed to carry a non-zero `safe` instead
# of relying on luck: it reports 5 failures on the pre-fix parser and passes on
# the fixed one.  See docs/AUDIT.md 1.48.
#
# Registered last so no existing #N moves.
add_executable(parser_member_safe_probe
  src/parser/parser.c src/lexer/lexer.c src/common/common.c
  src/parser/parser_member_safe_probe.c)
target_include_directories(parser_member_safe_probe PRIVATE src/parser src/lexer src/common)
if(NOT WIN32)
  target_compile_definitions(parser_member_safe_probe PRIVATE _GNU_SOURCE _stricmp=strcasecmp)
endif()
add_test(NAME parser_member_safe_probe COMMAND parser_member_safe_probe)
set_tests_properties(parser_member_safe_probe PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "parser;regression"
  PASS_REGULAR_EXPRESSION "parser_member_safe_probe: ok")

# ---------------------------------------------------------------------------
# Parameters and the working directory.  load_and_run() opens the parameter file
# AFTER chdir_to_script_dir() has moved the process into the script's directory,
# so a RELATIVE --params path was resolved against that directory instead of the
# caller's.  `inimerse --params vtest/p.params vtest/sub/s.im` therefore looked
# for `vtest/sub/vtest/p.params`, found nothing, and every parameter read as nil
# -- silently, with exit code 0.  The path is now pinned to an absolute one
# while the process is still in the caller's directory.
#
# The second pin is the sibling defect: a file the user NAMED with --params is
# not optional, so a miss is now an error instead of a silent nil.  The default
# `params.params` stays optional.
#
# The third pin is the precompiled path: vm_load_bytecode() rebinds globals by
# POSITION, so params loaded BEFORE the bytecode had their values attached to
# whichever name occupied that slot.  See docs/AUDIT.md 1.49.
#
# Registered last so no existing #N moves.
# NOTE: the --params arguments below are deliberately RELATIVE.  An absolute path
# would make the chdir invisible and the first test would pass against the bug.
add_test(NAME params_relative_path_runtime
         COMMAND inimerse --no-mods --params vtest/params_relative_v06.params
                 vtest/params_sub/relative_script.im)
set_tests_properties(params_relative_path_runtime PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;params;regression"
  PASS_REGULAR_EXPRESSION "params-relative speed=42")

add_test(NAME params_missing_explicit_runtime
         COMMAND inimerse --no-mods --params vtest/definitely_absent.params
                 vtest/params_sub/relative_script.im)
set_tests_properties(params_missing_explicit_runtime PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;params;regression"
  PASS_REGULAR_EXPRESSION "cannot read params"
  FAIL_REGULAR_EXPRESSION "params-relative speed=")

add_test(NAME params_precompiled_runtime
         COMMAND inimerse --no-mods --params vtest/params_precompiled_v06.params
                 vtest/params_precompiled_v06.inim)
set_tests_properties(params_precompiled_runtime PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;params;regression"
  PASS_REGULAR_EXPRESSION "params-precompiled player=42")

# spi_caps_contract_v06.im -- the capability string had two production points
# (posix_spi_meta's strstr chain and the Windows runtime's spi_parse_caps), and
# they answered "io net", "audio" and "ionet" differently.  Both now call the
# single vm_parse_caps.  Every string field is asserted once, because both
# platforms now agree on all of them; the caps argument's TYPE is still two
# production points (true -> 0 on POSIX, CAP_MASK on WIN32), so that ONE field
# is selected per platform here -- the divergence stays visible and
# change-detectable without anyone adjudicating it.  See docs/AUDIT.md 1.50.
#
# Registered last so no existing #N moves.
add_test(NAME spi_caps_contract_runtime
         COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/spi_caps_contract_v06.im)
set_tests_properties(spi_caps_contract_runtime PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;types;regression")
if(WIN32)
  set_tests_properties(spi_caps_contract_runtime PROPERTIES
    PASS_REGULAR_EXPRESSION "spi-caps-ok doc=768 space=0 audio=0 ionet=0 all=65280 int=256 bool=65280")
else()
  set_tests_properties(spi_caps_contract_runtime PROPERTIES
    PASS_REGULAR_EXPRESSION "spi-caps-ok doc=768 space=0 audio=0 ionet=0 all=65280 int=256 bool=0")
endif()

# docs/AUDIT.md 1.51 -- an atomic slot is an integer counter, and only that.
#
# atomic_get/atomic_add have two production points (src/runtime/runtime.c and
# src/runtime/runtime_posix.c) and neither consulted the slot's `type` tag
# before reading or writing the union.  atomic_get("y") on `y = 1.5` answered
# the IEEE754 bits of 1.5 on POSIX and 0 on Windows; atomic_add("h", 0) on
# `h = 2.5` destroyed the 2.5 on both, silently, exit code 0.
#
# The whole line is asserted because the defect exits 0 (docs/SYNTAX.md 7.2
# M13), and it is the SAME line on both platforms: both copies now consume one
# rule -- a slot that is not VAL_INT holds no integer counter, so the answer is
# 0 and the slot is left alone.  No per-platform branch, deliberately.
#
# Registered last so no existing #N moves.
add_test(NAME atomic_slot_type_contract_runtime
         COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/atomic_slot_type_contract_v06.im)
set_tests_properties(atomic_slot_type_contract_runtime PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;types;regression"
  PASS_REGULAR_EXPRESSION "atomic-slot-ok g1=0 r1=0 y=1.5 r3=0 yz=1.5 g2=0 r2=0 s=abcdef g3=7 r4=12 n1=type_mismatch n2=type_mismatch n3=type_mismatch n4=0"
  FAIL_REGULAR_EXPRESSION "n1=NO-THROW|n2=NO-THROW|n3=NO-THROW|n4=type_mismatch")

# docs/AUDIT.md 1.52 -- random(n) means an integer in [0, n), on both runtimes.
#
# `random` had two production points and only one of them was registered:
# src/runtime/runtime.c's builtin_random computed `rand() % max` but
# runtime_register_builtins never mentioned the name, while src/mod/io_mod.c
# registered its own copy that answered a bare `rand()` and ignored the
# argument.  On Windows that copy was the answer: random(10) returned 27606,
# random(0) returned the very same 27606, and the exit code was 0 both times.
#
# The io_mod copy is gone; the survivor is registered in
# runtime_register_builtins, which runs before register_core_modules
# (src/main.c:1096/1097).  There is now exactly one production point, so that
# ordering is no longer load-bearing.
#
# The whole line is asserted because the defect exits 0 (docs/SYNTAX.md 7.2
# M13).  Four of the five fields catch it and one does not, which is stated
# here rather than hidden: `big` is true on the pre-fix engine too, because a
# bare rand() is already smaller than 1000000.  Same line on both platforms, no
# per-platform branch: posix_random already had the bound and the `n > 0` gate,
# so the rule being consumed is not new, only newly shared.
#
# Registered last so no existing #N moves.
add_test(NAME random_bounded_contract_runtime
         COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/random_bounded_contract_v06.im)
set_tests_properties(random_bounded_contract_runtime PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;types;regression"
  PASS_REGULAR_EXPRESSION "random-bounded-ok one=true zero=true neg=true ten=true big=true")

# Pins "the array/dict pool refuses an index it does not own" (docs/AUDIT.md
# 1.54).  vm_pool_slot is the single door to the pool -- 71 call sites in src/
# go through it -- and eight of them test the answer for NULL.  The body had no
# bounds test at all, so it could never answer NULL: slot(-1) returned
# 0xffffffffffeefef0 on a fresh VM and a real heap address once arrays_big
# existed, slot(100000) returned 0x18e0a00, and only slot(4096) happened to be
# NULL (NULL + 0).  Every `if (!a)` downstream was dead code.
#
# A C probe rather than an .im script, and the reason is the point: a script
# cannot name a pool index, and the defect is in the C function, so this is the
# only level at which the contract is observable.  It links the engine objects
# directly, like vm_init_probe above.  It asserts the two ends of the inline
# pool still resolve as well, so a blanket `return NULL` cannot pass it, and it
# dereferences nothing -- the out-of-range answer is a pointer value, and on the
# pre-fix build that value is the evidence (probe prints `raw -1=...`).
#
# No PASS regex: the exit code is the verdict.
#
# Registered last so no existing #N moves.
add_executable(vm_pool_slot_probe src/vm/vm_pool_slot_probe.c)
target_link_libraries(vm_pool_slot_probe PRIVATE inimerse_engine)
if(WIN32)
  target_link_libraries(vm_pool_slot_probe PRIVATE m winhttp crypt32 gdi32 winmm msimg32
    windowscodecs ole32 ws2_32 comdlg32 shell32 shlwapi iphlpapi)
else()
  target_link_libraries(vm_pool_slot_probe PRIVATE m Threads::Threads ${CMAKE_DL_LIBS})
endif()
add_test(NAME vm_pool_slot_probe COMMAND vm_pool_slot_probe)

# Three cases that wire up evidence the docs already cited but that nothing ran.
#
# Registered after the contract pins above so that their documented #N -- cited in
# docs/AUDIT.md and docs/STATUS.md -- do not move.
#
# `docs/API.md` §2.1 had two `case`-coverage rows whose "证据（CTest）" column named
# vtest *files* -- vtest/lint_case_missing_default_v04.im and
# vtest/lint_case_exhaustive_v04.im -- and no CTest referenced either file, so the
# claim could only be checked by hand.  Both are registered here with the
# diagnostic each one is supposed to produce, and with a FAIL regex naming the
# *other* diagnostic: swapping them would be caught instead of silently accepted.
# The two fixtures are one line apart in what they exercise, so asserting only
# your own finding cannot tell "the diagnostic fired for the right reason" from
# "the diagnostic fired for every case".
#
# The names follow the fixture names, like the five hand-listed `lint_case_*`
# tests already registered: a fixture with no registration is a file whose output
# nothing compares, and nothing in the repository noticed the sixth and seventh
# were missing.  See docs/AUDIT.md §1.59.
#
# `tools/desugar_probe.sh` was the only executable evidence for the whole
# `--desugar` channel and the `say@target` rewrite, and nothing referenced it at
# all -- so it never ran.  It is now tools/desugar.test.py, which also runs on
# Windows where the shell probe could not.
if(INIMERSE_BUILD_ENGINE)
  # A `case` with no wildcard branch: the lint must say coverage cannot be proven.
  add_test(NAME lint_case_missing_default_runtime COMMAND inimerse --lint ${CMAKE_SOURCE_DIR}/vtest/lint_case_missing_default_v04.im)
  set_tests_properties(lint_case_missing_default_runtime PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;lint;case"
    PASS_REGULAR_EXPRESSION "case has no wildcard '_'/'else' branch"
    FAIL_REGULAR_EXPRESSION "case branch is unreachable")
  # A wildcard placed before another branch: the branch after it is unreachable.
  add_test(NAME lint_case_exhaustive_runtime COMMAND inimerse --lint ${CMAKE_SOURCE_DIR}/vtest/lint_case_exhaustive_v04.im)
  set_tests_properties(lint_case_exhaustive_runtime PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;lint;case"
    PASS_REGULAR_EXPRESSION "case branch is unreachable: wildcard '_'/'else' appears before this branch"
    FAIL_REGULAR_EXPRESSION "case has no wildcard")
  # `--desugar`: `say@target expr` -> `say_target("target", expr)`, plus the
  # negative control that the engine refuses the sugar without the desugarer.
  add_test(NAME desugar_runtime COMMAND ${INIMERSE_TEST_PYTHON} ${CMAKE_SOURCE_DIR}/tools/desugar.test.py $<TARGET_FILE:inimerse>)
  set_tests_properties(desugar_runtime PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "language;desugar"
    ENVIRONMENT "PYTHONIOENCODING=utf-8"
    PASS_REGULAR_EXPRESSION "desugar tests: ok")
  # count(A) is the canonical name for a collection's cardinality; size() is the
  # deprecated set-context spelling of it (2026-10 ruling, docs/TYPESET_V06.md
  # 7.8).  This pin separates the three distinct routes to nil -- a
  # non-collection (count(42)), a negative integer through size()'s own `if
  # (n < 0)` tail, and a collection the enumerator refuses (count(Z)) -- and
  # keeps the retained size() coercions (size(42) = 42, size(3.7) = 3) and the
  # arity trap where only the LAST argument of a builtin call is read
  # (size(5, 5, 5) is size(5)).  Values only, never exit codes.
  add_test(NAME count_builtin_runtime COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/count_builtin_v06.im)
  set_tests_properties(count_builtin_runtime PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;types;regression"
    PASS_REGULAR_EXPRESSION "count-ok bad=0 set=3,5,5,5 dict=2,2 str=5,5 arr=3,3 int=nil,42 float=nil,3 neg=nil,nil unenum=nil,nil arity=nil,5,9"
    FAIL_REGULAR_EXPRESSION "bad=[1-9]")
endif()
