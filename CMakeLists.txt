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
add_test(NAME verse_closed_loop
  COMMAND ${CMAKE_COMMAND} -E env INIM_SERVER_BIN=$<TARGET_FILE:inim-server>
          python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/verse_closed_loop.test.py
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
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/crp_closed_loop.test.py
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

add_executable(fiber_probe src/platform/fiber.c src/platform/fiber_probe.c)
if(NOT WIN32)
  target_link_libraries(fiber_probe PRIVATE Threads::Threads)
endif()
add_executable(process_probe src/platform/process.c src/platform/process_probe.c)
if(NOT WIN32)
  add_executable(crp_session_probe src/platform/crp_session.c src/common/sha256.c src/platform/crp_session_probe.c)
  add_test(NAME crp_session_probe COMMAND crp_session_probe)
endif()
add_executable(socket_probe src/platform/socket.c src/platform/socket_probe.c)
if(WIN32)
  target_link_libraries(socket_probe PRIVATE ws2_32)
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
  add_executable(http_probe src/platform/socket.c src/platform/dir.c src/platform/websocket.c src/platform/crp_session.c src/common/sha256.c src/platform/http_posix.c src/verse/json_min.c src/verse/crp.c src/verse/upp.c src/common/ed25519.c src/platform/http_client.c src/platform/http_probe.c)
  target_link_libraries(http_probe PRIVATE Threads::Threads)
  target_include_directories(http_probe PRIVATE src/verse src/common src/platform)
  add_test(NAME http_probe COMMAND bash ${CMAKE_SOURCE_DIR}/tools/http_probe_retry.sh $<TARGET_FILE:http_probe>)
  add_executable(hub_probe src/platform/socket.c src/platform/dir.c src/platform/websocket.c src/platform/crp_session.c src/common/sha256.c src/platform/http_posix.c src/verse/json_min.c src/verse/crp.c src/verse/upp.c src/common/ed25519.c src/platform/http_client.c src/platform/hub_probe.c)
  target_link_libraries(hub_probe PRIVATE Threads::Threads)
  target_include_directories(hub_probe PRIVATE src/verse src/common src/platform)
  add_test(NAME hub_probe COMMAND hub_probe)
  add_executable(websocket_probe src/platform/socket.c src/platform/dir.c src/platform/websocket.c src/platform/crp_session.c src/common/sha256.c src/platform/http_posix.c src/verse/json_min.c src/verse/crp.c src/verse/upp.c src/common/ed25519.c src/platform/http_client.c src/platform/websocket_probe.c)
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
find_program(INIMERSE_PYTHON NAMES python3 python)
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
  add_test(NAME bindgen_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/bindgen.test.py)
  set_tests_properties(bindgen_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "compiler;regression")
  add_test(NAME wasm_backend_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/wasm_backend.test.py)
  set_tests_properties(wasm_backend_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120 LABELS "compiler;regression;wasm")
  add_test(NAME scan_tools_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/scan_tools.test.py)
  set_tests_properties(scan_tools_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "compiler;regression")
  add_test(NAME replay_closure_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/replay.test.py)
  set_tests_properties(replay_closure_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "runtime;regression")
  set_tests_properties(cli_incremental_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60 LABELS "compiler;regression")
  add_test(NAME release_verify_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/release_verify.test.py)
  set_tests_properties(release_verify_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "release;regression")
  # Oversized HTTP file bodies must be refused (413), never truncated, and
  # `--port 0` / `--http-port 0` must be reported by the engine.  Needs only
  # Python and the engine binary, so it belongs to the Python-gated block.
  add_test(NAME http_large_package_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/http_large_package.test.py)
  set_tests_properties(http_large_package_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180 LABELS "protocol;regression")
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
    src/vm/vm.c src/vm/params_v2.c src/vm/jit_mode.c)
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
    # src/parser/parser.c and src/compiler/compiler.c) and the only definition
    # in the tree lives in src/main.c.  A shared library has to resolve it, so
    # both bridge targets compile that same file - the same source, not a copy -
    # with the CLI entry point renamed, so a Python extension does not export a
    # `main`.  Nothing else in src/main.c is reachable from the bridge.
    # --- the native library the generated Java facade loads ---------------
    add_library(inimerse_bridge SHARED
      src/bridge/bridge_abi.c src/bridge/bridge_jni.c src/main.c "${XLANG_ABI_HEADER}")
    target_compile_definitions(inimerse_bridge PRIVATE main=inimerse_bridge_entry_unused)
    set_target_properties(inimerse_bridge PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY "${XLANG_BRIDGE_DIR}")
    target_include_directories(inimerse_bridge PRIVATE
      "${CMAKE_BINARY_DIR}/bindings/c"
      "${XLANG_JAVA_HOME}/include" "${XLANG_JAVA_HOME}/include/linux")
    target_link_libraries(inimerse_bridge PRIVATE inimerse_engine)

    # --- the CPython extension the wheel ships ----------------------------
    add_library(inimerse_python MODULE
      src/bridge/inimerse_extension.c src/bridge/bridge_abi.c src/main.c "${XLANG_ABI_HEADER}")
    target_compile_definitions(inimerse_python PRIVATE main=inimerse_python_entry_unused)
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
  set_tests_properties(type_collection_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;types;collection")
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/range_meta_sandbox)
  add_test(NAME range_meta_runtime
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/range_meta_sandbox
      $<TARGET_FILE:inimerse> --no-mods ${CMAKE_SOURCE_DIR}/vtest/range_meta_v04.im)
  set_tests_properties(range_meta_runtime PROPERTIES TIMEOUT 15 LABELS "language;types;metadata" PASS_REGULAR_EXPRESSION "range-meta-pass")
  add_test(NAME posix_core_api_runtime COMMAND inimerse --no-mods ${CMAKE_SOURCE_DIR}/vtest/posix_core_api_v04.im)
  set_tests_properties(posix_core_api_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;collection;strings" PASS_REGULAR_EXPRESSION "posix-core-api-ok")
  add_test(NAME posix_runtime_parity COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/posix_runtime_parity_v04.im)
  if(NOT WIN32)
    set_tests_properties(posix_runtime_parity PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "runtime;posix" PASS_REGULAR_EXPRESSION "posix-runtime-parity-ok" FAIL_REGULAR_EXPRESSION "posix-runtime-parity-fail")
  else()
    set_tests_properties(posix_runtime_parity PROPERTIES DISABLED TRUE)
  endif()
  add_test(NAME gc_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/gc_runtime_v04.im)
  set_tests_properties(gc_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;memory;gc" PASS_REGULAR_EXPRESSION "gc-ran;gc-stats-ok;thread-result-alive;closure-capture-alive;root-alive;gc-done")
  add_test(NAME pipeline_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/pipeline_v04.im)
  set_tests_properties(pipeline_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar")
  add_test(NAME result_question_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/result_question_v04.im)
  # Assert on the marked VALUE line, not on the unreachable literal: the engine
  # echoes every string literal, so FAIL_REGULAR_EXPRESSION "resultq unreachable"
  # matched the echo even when the statement never ran.
  set_tests_properties(result_question_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;result" PASS_REGULAR_EXPRESSION "resultq value=42.*resultq caught.*resultq reached=0")
  add_test(NAME null_coalesce_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/null_coalesce_v04.im)
  set_tests_properties(null_coalesce_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;null-safety")
  add_test(NAME optional_member_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/optional_member_v04.im)
  # The engine echoes builtin call arguments, so the dict literal's own
  # "inimerse" appears in the output even when `?.` is broken -- assert on the
  # OM-prefixed value lines instead. PASS catches the key being truncated
  # (bytecode_add_string got a non-NUL-terminated StringView); FAIL catches the
  # stale recycled register that made nil?.name return the previous result.
  set_tests_properties(optional_member_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;null-safety" PASS_REGULAR_EXPRESSION "OM present=inimerse" FAIL_REGULAR_EXPRESSION "OM missing=inimerse")
  add_test(NAME chained_comparison_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/chained_comparison_v04.im)
  set_tests_properties(chained_comparison_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;comparison")
  add_test(NAME postfix_condition_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/postfix_condition_v04.im)
  set_tests_properties(postfix_condition_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;control-flow")
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
  add_test(NAME case_collection_patterns_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_collection_patterns_v04.im)
  set_tests_properties(case_collection_patterns_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;collection;types")
  add_test(NAME case_structural_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_structural_v04.im)
  set_tests_properties(case_structural_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;structure")
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
  set_tests_properties(float_precision_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;numeric")
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
  install(PROGRAMS tools/inim.py DESTINATION bin RENAME inim)
  install(FILES README.md LICENSE DESTINATION share/doc/inimerse)
else()
  message(STATUS "Building portable platform probes only; platform-specific VM modules remain disabled")
endif()
