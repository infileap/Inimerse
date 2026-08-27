cmake_minimum_required(VERSION 3.20)
project(inimerse LANGUAGES C)
enable_testing()

set(CPACK_PACKAGE_NAME "inimerse")
set(CPACK_PACKAGE_VENDOR "Infileap")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Portable Inimerse VM")
set(CPACK_PACKAGE_VERSION "0.3.0")
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

add_library(inimerse_platform STATIC src/platform/platform.c src/platform/thread.c src/platform/fiber.c src/platform/process.c src/platform/socket.c src/platform/dir.c)
target_include_directories(inimerse_platform PUBLIC src)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(inimerse_platform PUBLIC Threads::Threads)
endif()

add_executable(platform_probe src/platform/platform_probe.c)
target_link_libraries(platform_probe PRIVATE inimerse_platform)

add_executable(fiber_probe src/platform/fiber.c src/platform/fiber_probe.c)
if(NOT WIN32)
  target_link_libraries(fiber_probe PRIVATE Threads::Threads)
endif()
add_executable(process_probe src/platform/process.c src/platform/process_probe.c)
add_executable(socket_probe src/platform/socket.c src/platform/socket_probe.c)
if(WIN32)
  target_link_libraries(socket_probe PRIVATE ws2_32)
endif()
add_test(NAME platform_probe COMMAND platform_probe)
add_test(NAME fiber_probe COMMAND fiber_probe)
add_test(NAME process_probe COMMAND process_probe)
add_test(NAME socket_probe COMMAND socket_probe)
set_tests_properties(platform_probe fiber_probe process_probe socket_probe
  PROPERTIES TIMEOUT 10 LABELS "platform;smoke")
find_program(INIMERSE_NODE node)
if(INIMERSE_NODE)
  add_test(NAME protocol_regression COMMAND ${INIMERSE_NODE} tools/regression.js)
  set_tests_properties(protocol_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 30 LABELS "protocol;regression")
endif()

# The full engine is intentionally enabled only after the VM's thread/fiber
# backend is ported.  Keeping this gate explicit prevents a misleading build
# that silently omits platform modules on POSIX systems.
if(INIMERSE_BUILD_ENGINE)
  set(INIMERSE_ENGINE_SOURCES
    src/main.c src/platform/platform.c src/platform/thread.c src/platform/fiber.c
    src/platform/dir.c
    src/common/common.c src/common/sha256.c src/common/ed25519.c
    src/lexer/lexer.c src/parser/parser.c
    src/compiler/bytecode.c src/compiler/compiler.c
    src/vm/vm.c)
  if(WIN32)
    list(APPEND INIMERSE_ENGINE_SOURCES
      src/runtime/runtime.c src/child_proc.c src/headless_server.c
      src/isolate_mod.c src/desugar_mod.c src/lint_mod.c src/mod/mod.c
      src/mod/gui_mod.c src/mod/io_mod.c src/mod/net_mod.c src/mod/json_mod.c
      src/mod/record_mod.c src/mod/infiverse_mod.c src/mod/verse_dist_mod.c
      src/mod/server_mod.c src/mod/identity_mod.c src/mod/social_mod.c
      src/mod/ai_mod.c mods/build/build_mod.c)
  else()
    list(APPEND INIMERSE_ENGINE_SOURCES src/runtime/runtime_posix.c
      src/mod/mod_posix.c src/mod/net_mod_posix.c src/platform/socket.c
      src/platform/process.c src/isolate_mod.c
      src/platform/posix_stubs.c)
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
  install(FILES README.md LICENSE DESTINATION share/doc/inimerse)
else()
  message(STATUS "Building portable platform probes only; platform-specific VM modules remain disabled")
endif()
