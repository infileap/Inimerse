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
    src/main.c src/platform/platform.c src/platform/thread.c src/platform/fiber.c
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
  add_executable(inimerse ${INIMERSE_ENGINE_SOURCES})
  target_include_directories(inimerse PRIVATE src src/parser src/compiler src/vm
    src/runtime src/mod src/common src/lexer src/platform src/verse)
  if(NOT WIN32)
    target_compile_definitions(inimerse PRIVATE
      _GNU_SOURCE _stricmp=strcasecmp
      INIMERSE_VERSION_STRING="${INIMERSE_PACKAGE_VERSION}")
  else()
    target_compile_definitions(inimerse PRIVATE
      INIMERSE_VERSION_STRING="${INIMERSE_PACKAGE_VERSION}")
  endif()
  if(WIN32)
    target_link_libraries(inimerse PRIVATE m winhttp crypt32 gdi32 winmm msimg32
      windowscodecs ole32 ws2_32 comdlg32 shell32 shlwapi iphlpapi)
  else()
    target_link_libraries(inimerse PRIVATE m Threads::Threads ${CMAKE_DL_LIBS})
  endif()
  install(TARGETS inimerse RUNTIME DESTINATION bin)
  add_test(NAME version_runtime COMMAND inimerse --version)
  set_tests_properties(version_runtime PROPERTIES
    TIMEOUT 15 LABELS "release;metadata"
    PASS_REGULAR_EXPRESSION "inimerse ${INIMERSE_PACKAGE_VERSION}")
  add_test(NAME result_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/result_v04.im)
  set_tests_properties(result_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;result")
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
  set_tests_properties(result_question_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;result")
  add_test(NAME null_coalesce_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/null_coalesce_v04.im)
  set_tests_properties(null_coalesce_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;null-safety")
  add_test(NAME optional_member_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/optional_member_v04.im)
  set_tests_properties(optional_member_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;null-safety")
  add_test(NAME chained_comparison_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/chained_comparison_v04.im)
  set_tests_properties(chained_comparison_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;comparison")
  add_test(NAME postfix_condition_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/postfix_condition_v04.im)
  set_tests_properties(postfix_condition_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;control-flow")
  add_test(NAME result_propagation_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/result_propagation_v04.im)
  set_tests_properties(result_propagation_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;result")
  add_test(NAME result_finally_propagation_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/result_finally_propagation_probe_v04.im)
  set_tests_properties(result_finally_propagation_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;error-handling;result" PASS_REGULAR_EXPRESSION "finally-ran" FAIL_REGULAR_EXPRESSION "inner")
  add_test(NAME try_finally_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/try_finally_v04.im)
  set_tests_properties(try_finally_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;error-handling")
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
  set_tests_properties(case_alias_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;structure")
  add_test(NAME case_array_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_array_v04.im)
  set_tests_properties(case_array_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;structure" PASS_REGULAR_EXPRESSION "30.*20")
  add_test(NAME case_nested_patterns_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_nested_patterns_v04.im)
  set_tests_properties(case_nested_patterns_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;structure" PASS_REGULAR_EXPRESSION "53;nested-fallback-ok;nested-literal-ok")
  add_test(NAME lambda_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/lambda_v04.im)
  add_test(NAME composition_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/composition_v04.im)
  set_tests_properties(composition_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional" PASS_REGULAR_EXPRESSION "8")
  add_test(NAME lambda_capture_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/lambda_capture_rejected_v04.im)
  set_tests_properties(lambda_capture_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional")
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
  set_tests_properties(lambda_nested_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional" PASS_REGULAR_EXPRESSION "9")
  set_tests_properties(lambda_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional")
  add_test(NAME float_precision_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/float_precision_v04.im)
  set_tests_properties(float_precision_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;numeric")
  install(PROGRAMS tools/inim.py DESTINATION bin RENAME inim)
  install(FILES README.md LICENSE DESTINATION share/doc/inimerse)
else()
  message(STATUS "Building portable platform probes only; platform-specific VM modules remain disabled")
endif()
