cmake_minimum_required(VERSION 3.20)
project(inimerse LANGUAGES C)
enable_testing()

set(CPACK_PACKAGE_NAME "inimerse")
set(CPACK_PACKAGE_VENDOR "Infileap")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Portable Inimerse VM")
set(INIMERSE_PACKAGE_VERSION "0.3.0" CACHE STRING "Package/release version")
set(CPACK_PACKAGE_VERSION "${INIMERSE_PACKAGE_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "inimerse-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ;ZIP;DEB")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
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
add_executable(bytecode_capture_probe src/compiler/bytecode.c src/compiler/bytecode_capture_probe.c)
target_include_directories(bytecode_capture_probe PRIVATE src src/compiler src/common)
target_link_libraries(bytecode_capture_probe PRIVATE inimerse_platform)
add_test(NAME bytecode_capture_probe COMMAND bytecode_capture_probe)
add_executable(closure_probe src/vm/closure.c src/vm/closure_probe.c)
target_include_directories(closure_probe PRIVATE src/vm src src/compiler src/common)
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
  add_executable(crp_session_probe src/platform/crp_session.c src/platform/crp_session_probe.c)
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
  add_executable(http_probe src/platform/socket.c src/platform/dir.c src/platform/websocket.c src/platform/crp_session.c src/common/sha256.c src/platform/http_posix.c src/platform/http_client.c src/platform/http_probe.c)
  target_link_libraries(http_probe PRIVATE Threads::Threads)
  add_test(NAME http_probe COMMAND http_probe)
  add_executable(hub_probe src/platform/socket.c src/platform/dir.c src/platform/websocket.c src/platform/crp_session.c src/common/sha256.c src/platform/http_posix.c src/platform/http_client.c src/platform/hub_probe.c)
  target_link_libraries(hub_probe PRIVATE Threads::Threads)
  add_test(NAME hub_probe COMMAND hub_probe)
  add_executable(websocket_probe src/platform/socket.c src/platform/dir.c src/platform/websocket.c src/platform/crp_session.c src/common/sha256.c src/platform/http_posix.c src/platform/http_client.c src/platform/websocket_probe.c)
  target_link_libraries(websocket_probe PRIVATE Threads::Threads)
endif()
set_tests_properties(platform_probe fiber_probe process_probe socket_probe thread_probe dir_probe
  PROPERTIES TIMEOUT 10 LABELS "platform;smoke" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
if(NOT WIN32)
  set_tests_properties(headless_probe PROPERTIES TIMEOUT 10 LABELS "platform;smoke;headless")
  set_tests_properties(http_probe hub_probe PROPERTIES TIMEOUT 10 LABELS "platform;smoke;http" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(crp_session_probe PROPERTIES TIMEOUT 10 LABELS "platform;smoke;crp")
endif()
find_program(INIMERSE_NODE node)
find_program(INIMERSE_PYTHON NAMES python3 python)
if(INIMERSE_PYTHON)
  add_test(NAME inim_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/inim.test.py)
  set_tests_properties(inim_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "package;regression")
  add_test(NAME release_verify_regression COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/release_verify.test.py)
  set_tests_properties(release_verify_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30 LABELS "release;regression")
endif()
if(INIMERSE_NODE)
  add_test(NAME protocol_regression COMMAND ${INIMERSE_NODE} tools/regression.js)
  set_tests_properties(protocol_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 30 LABELS "protocol;regression")
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
    src/types/typeset.c src/types/enum.c src/types/error_types.c src/types/registry.c
    src/vm/closure.c
    src/lexer/lexer.c src/parser/parser.c
    src/compiler/bytecode.c src/compiler/compiler.c
    src/vm/vm.c src/vm/params_v2.c src/vm/jit_mode.c)
  if(WIN32)
    list(APPEND INIMERSE_ENGINE_SOURCES
      src/runtime/runtime.c src/child_proc.c src/headless_server.c
      src/isolate_mod.c src/desugar_mod.c src/lint_mod.c src/mod/mod.c
      src/mod/gui_mod.c src/mod/io_mod.c src/mod/net_mod.c src/mod/json_mod.c
      src/mod/record_mod.c src/mod/infiverse_mod.c src/mod/verse_dist_mod.c
      src/mod/server_mod.c src/mod/identity_mod.c src/mod/social_mod.c
      src/mod/ai_mod.c src/mod/result_mod.c mods/build/build_mod.c)
    list(APPEND INIMERSE_ENGINE_SOURCES src/mod/say_mod_windows.c src/mod/say_stream.c)
  else()
    list(APPEND INIMERSE_ENGINE_SOURCES src/runtime/runtime_posix.c
      src/mod/mod_posix.c src/mod/net_mod_posix.c src/mod/server_mod_posix.c src/mod/say_mod_posix.c src/mod/say_stream.c src/platform/socket.c
      src/platform/process.c src/isolate_mod.c
      src/platform/posix_stubs.c src/platform/http_posix.c src/platform/http_client.c src/platform/serial_posix.c src/platform/websocket.c src/platform/crp_session.c src/desugar_mod.c src/lint_mod.c
      src/headless_server_posix.c src/mod/result_mod.c)
  endif()
  add_executable(inimerse ${INIMERSE_ENGINE_SOURCES})
  target_include_directories(inimerse PRIVATE src src/parser src/compiler src/vm
    src/runtime src/mod src/common src/lexer)
  if(NOT WIN32)
    target_compile_definitions(inimerse PRIVATE _GNU_SOURCE _stricmp=strcasecmp)
  endif()
  if(WIN32)
    target_link_libraries(inimerse PRIVATE m winhttp crypt32 gdi32 winmm msimg32
      windowscodecs ole32 ws2_32 comdlg32 shell32 shlwapi iphlpapi)
  else()
    target_link_libraries(inimerse PRIVATE m Threads::Threads ${CMAKE_DL_LIBS})
  endif()
  install(TARGETS inimerse RUNTIME DESTINATION bin)
  add_test(NAME result_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/result_v04.im)
  set_tests_properties(result_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;result")
  add_test(NAME collection_comprehension_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/collection_comprehension_v04.im)
  set_tests_properties(collection_comprehension_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar")
  add_test(NAME type_collection_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/type_collection_v04.im)
  set_tests_properties(type_collection_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;types;collection")
  add_test(NAME pipeline_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/pipeline_v04.im)
  set_tests_properties(pipeline_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar")
  add_test(NAME result_question_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/result_question_v04.im)
  set_tests_properties(result_question_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;result")
  add_test(NAME case_try_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_try_v04.im)
  set_tests_properties(case_try_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;result;case")
  add_test(NAME case_collection_patterns_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_collection_patterns_v04.im)
  set_tests_properties(case_collection_patterns_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;collection;types")
  add_test(NAME case_structural_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/case_structural_v04.im)
  set_tests_properties(case_structural_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;case;structure")
  add_test(NAME lambda_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/lambda_v04.im)
  add_test(NAME composition_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/composition_v04.im)
  set_tests_properties(composition_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional")
  add_test(NAME lambda_capture_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/lambda_capture_rejected_v04.im)
  set_tests_properties(lambda_capture_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional")
  set_tests_properties(lambda_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;syntax-sugar;functional")
  add_test(NAME float_precision_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/float_precision_v04.im)
  set_tests_properties(float_precision_runtime PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15 LABELS "language;numeric")
  install(PROGRAMS tools/inim.py DESTINATION bin RENAME inim)
  install(FILES README.md LICENSE DESTINATION share/doc/inimerse)
else()
  message(STATUS "Building portable platform probes only; platform-specific VM modules remain disabled")
endif()
