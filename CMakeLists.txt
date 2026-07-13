cmake_minimum_required(VERSION 3.28)
project(IOProject VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/modern_runtime_provider.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/modern_trace_provider.cmake)

set(MODERN_IO_PACKAGE_NAME modern_io)
set(MODERN_IO_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${MODERN_IO_PACKAGE_NAME}")
set(MODERN_IO_PACKAGE_VERSION "${PROJECT_VERSION}")
set(MODERN_IO_DEPENDENCIES_DIR "${CMAKE_CURRENT_BINARY_DIR}/dependencies" CACHE PATH "Directory for dependency git checkouts")

# ----------------------------
# 1) General C++23 settings
# ----------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_VERBOSE_MAKEFILE ON)

# ----------------------------
# 2) Compiler detection and module flags
# ----------------------------
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "✅ Clang detected: enabling module scan and recommended flags")
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
    add_compile_options(
    #-stdlib=libc++            # REMOVED: enable only when libc++ is installed
        -Wno-unused-command-line-argument
        -Wno-unknown-warning-option
        -Wno-missing-braces
        # Clang 18 reports __cpp_concepts=201907L (old draft) in C++ module mode,
        # which causes GCC's <expected> header guard to fail. Override with the
        # correct C++20 final value so <expected> is visible in global module fragments.
        -D__cpp_concepts=202002L
        -Wno-builtin-macro-redefined
    )
    # add_link_options(-stdlib=libc++)  # REMOVED
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING "⚠️ GCC detected: enabling experimental modules, manual ordering may be needed")
    add_compile_options(-fmodules-ts)
endif()

# Always explicitly find/link threads
find_package(Threads REQUIRED)

set(MODERN_RUNTIME_PROVIDER "fetch" CACHE STRING "How to resolve modern_runtime: package, local, or fetch")
set_property(CACHE MODERN_RUNTIME_PROVIDER PROPERTY STRINGS package local fetch)
set(MODERN_RUNTIME_ROOT "${MODERN_IO_DEPENDENCIES_DIR}/modern_runtime" CACHE PATH "Path to a local modern_runtime checkout")
set(MODERN_RUNTIME_SOURCE_DIR "${MODERN_IO_DEPENDENCIES_DIR}/modern_runtime" CACHE PATH "FetchContent source checkout directory for modern_runtime")
set(MODERN_RUNTIME_GIT_REPOSITORY "https://github.com/markushocke/modern_runtime.git" CACHE STRING "Git repository used when MODERN_RUNTIME_PROVIDER=fetch")
set(MODERN_RUNTIME_GIT_TAG "main" CACHE STRING "Git tag or branch used when MODERN_RUNTIME_PROVIDER=fetch")
set(MODERN_TRACE_PROVIDER "fetch" CACHE STRING "How to resolve modern_trace: auto, package, local, or fetch")
set_property(CACHE MODERN_TRACE_PROVIDER PROPERTY STRINGS auto package local fetch)
set(MODERN_TRACE_ROOT "${MODERN_IO_DEPENDENCIES_DIR}/modern_trace" CACHE PATH "Path to a local modern_trace checkout")
set(MODERN_TRACE_SOURCE_DIR "${MODERN_IO_DEPENDENCIES_DIR}/modern_trace" CACHE PATH "FetchContent source checkout directory for modern_trace")
set(MODERN_TRACE_GIT_REPOSITORY "https://github.com/markushocke/modern_trace.git" CACHE STRING "Git repository used when MODERN_TRACE_PROVIDER=fetch")
set(MODERN_TRACE_GIT_TAG "main" CACHE STRING "Git tag or branch used when MODERN_TRACE_PROVIDER=fetch")

modern_io_resolve_trace_target(
  MODERN_TRACE_TARGET
  DEFAULT_PROVIDER "auto"
  LOCAL_ROOT "${MODERN_TRACE_ROOT}"
  FETCH_REPOSITORY "${MODERN_TRACE_GIT_REPOSITORY}"
  FETCH_TAG "${MODERN_TRACE_GIT_TAG}"
  SOURCE_DIR "${MODERN_TRACE_SOURCE_DIR}"
  BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/modern_trace"
)

modern_io_resolve_runtime_target(
  MODERN_RUNTIME_TARGET
  DEFAULT_PROVIDER "fetch"
  LOCAL_ROOT "${MODERN_RUNTIME_ROOT}"
  FETCH_REPOSITORY "${MODERN_RUNTIME_GIT_REPOSITORY}"
  FETCH_TAG "${MODERN_RUNTIME_GIT_TAG}"
  SOURCE_DIR "${MODERN_RUNTIME_SOURCE_DIR}"
  BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/modern_runtime"
)

set(MODERN_RUNTIME_INSTALL_TARGET modern_runtime::modern_runtime)
set(MODERN_RUNTIME_PUBLIC_LINK
  $<BUILD_INTERFACE:${MODERN_RUNTIME_TARGET}>
  $<INSTALL_INTERFACE:${MODERN_RUNTIME_INSTALL_TARGET}>
)

macro(add_module_target target)
    if (MSVC)
    set_target_properties(${target} PROPERTIES
            CXX_SCAN_FOR_MODULES YES
            CXX_MODULE_DIRECTORY "${CMAKE_CXX_MODULE_DIRECTORY}/${target}"
        )
    endif()
endmacro()

# ----------------------------
# 3) modern_io Library (sync)
# ----------------------------
add_library(modern_io)
target_sources(modern_io
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/modern_io.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/exceptions.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/stream.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/concepts.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/connection_arena.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/file.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/data.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/buffered.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/iostream.cppm
)
target_compile_features(modern_io PUBLIC cxx_std_23)
target_link_libraries(modern_io PUBLIC Threads::Threads ${MODERN_TRACE_TARGET})
add_module_target(modern_io)
add_library(modern_io::modern_io ALIAS modern_io)

# ----------------------------
# 4) modern_io_async Library
# ----------------------------
add_library(modern_io_async)
target_sources(modern_io_async
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/async/modern_io_async.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/async/task.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/async/data.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/async/buffered.cppm
)
target_compile_features(modern_io_async PUBLIC cxx_std_23)
target_link_libraries(modern_io_async PUBLIC modern_io Threads::Threads ${MODERN_RUNTIME_PUBLIC_LINK})
add_module_target(modern_io_async)
add_library(modern_io::modern_io_async ALIAS modern_io_async)

# ----------------------------
# 5) net_io Library (sync)
# ----------------------------
add_library(net_io)
target_sources(net_io
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/net_io.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/exceptions.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/base.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/concepts.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/tcp/endpoint.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/tcp/client.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/tcp/server.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/udp/endpoint.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/udp/transport.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/ipc/transport.cppm
)
target_link_libraries(net_io PUBLIC modern_io Threads::Threads)
target_compile_features(net_io PUBLIC cxx_std_23)
add_module_target(net_io)
add_library(modern_io::net_io ALIAS net_io)

# ----------------------------
# 6) net_io_async Library
# ----------------------------
add_library(net_io_async)
target_sources(net_io_async
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/net_io_async.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/task.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/concepts.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/event_loop.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/io_awaiter.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/tcp_socket.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/udp_socket.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/stream_base.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/stream_adapters.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/reactor_ops.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/tcp_server.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/scheduler.cppm
  PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/event_loop.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/udp_socket.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/tcp_socket.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/tcp_server.cpp
)
target_link_libraries(net_io_async PUBLIC net_io modern_io_async Threads::Threads ${MODERN_RUNTIME_PUBLIC_LINK})
target_compile_features(net_io_async PUBLIC cxx_std_23)
add_module_target(net_io_async)
add_library(modern_io::net_io_async ALIAS net_io_async)

# ----------------------------
# 7) net_io_adapters Library
# ----------------------------
add_library(net_io_adapters)
target_sources(net_io_adapters
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/adapters/net_io_adapters.cppm
)
target_link_libraries(net_io_adapters PUBLIC modern_io net_io Threads::Threads)
target_compile_features(net_io_adapters PUBLIC cxx_std_23)
add_module_target(net_io_adapters)
add_library(modern_io::net_io_adapters ALIAS net_io_adapters)

# ----------------------------
# 8) Canonical modern.* module targets
# ----------------------------
add_library(modern_io_canonical)
target_sources(modern_io_canonical
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/modern.io.cppm
)
target_compile_features(modern_io_canonical PUBLIC cxx_std_23)
target_link_libraries(modern_io_canonical PUBLIC modern_io Threads::Threads)
add_module_target(modern_io_canonical)
add_library(modern_io::modern_io_canonical ALIAS modern_io_canonical)

add_library(modern_io_async_canonical)
target_sources(modern_io_async_canonical
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/async/modern.io.async.cppm
)
target_compile_features(modern_io_async_canonical PUBLIC cxx_std_23)
target_link_libraries(modern_io_async_canonical PUBLIC modern_io modern_io_async Threads::Threads ${MODERN_RUNTIME_PUBLIC_LINK})
add_module_target(modern_io_async_canonical)
add_library(modern_io::modern_io_async_canonical ALIAS modern_io_async_canonical)

add_library(modern_net_canonical)
target_sources(modern_net_canonical
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/modern.net.cppm
)
target_compile_features(modern_net_canonical PUBLIC cxx_std_23)
target_link_libraries(modern_net_canonical PUBLIC net_io Threads::Threads)
add_module_target(modern_net_canonical)
add_library(modern_io::modern_net_canonical ALIAS modern_net_canonical)

add_library(modern_net_async_canonical)
target_sources(modern_net_async_canonical
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/modern.net.async.cppm
)
target_compile_features(modern_net_async_canonical PUBLIC cxx_std_23)
target_link_libraries(modern_net_async_canonical PUBLIC net_io net_io_async modern_io_async Threads::Threads ${MODERN_RUNTIME_PUBLIC_LINK})
add_module_target(modern_net_async_canonical)
add_library(modern_io::modern_net_async_canonical ALIAS modern_net_async_canonical)

add_library(modern_net_adapters_canonical)
target_sources(modern_net_adapters_canonical
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/adapters/modern.net.adapters.cppm
)
target_compile_features(modern_net_adapters_canonical PUBLIC cxx_std_23)
target_link_libraries(modern_net_adapters_canonical PUBLIC net_io_adapters Threads::Threads)
add_module_target(modern_net_adapters_canonical)
add_library(modern_io::modern_net_adapters_canonical ALIAS modern_net_adapters_canonical)

add_library(modern_all_canonical)
target_sources(modern_all_canonical
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/modern_io.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/modern.io.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/exceptions.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/stream.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/concepts.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/connection_arena.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/file.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/data.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/buffered.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/iostream.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/async/modern_io_async.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/async/modern.io.async.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/async/task.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/async/data.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/io/async/buffered.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/net_io.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/modern.net.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/exceptions.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/base.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/concepts.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/tcp/endpoint.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/tcp/client.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/tcp/server.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/udp/endpoint.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/udp/transport.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/ipc/transport.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/net_io_async.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/modern.net.async.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/task.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/concepts.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/event_loop.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/io_awaiter.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/tcp_socket.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/udp_socket.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/stream_base.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/stream_adapters.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/reactor_ops.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/tcp_server.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/scheduler.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/adapters/net_io_adapters.cppm
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/adapters/modern.net.adapters.cppm
  PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/event_loop.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/udp_socket.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/tcp_socket.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/modules/net/async/tcp_server.cpp
)
target_compile_features(modern_all_canonical PUBLIC cxx_std_23)
target_link_libraries(modern_all_canonical PUBLIC Threads::Threads ${MODERN_TRACE_TARGET} ${MODERN_RUNTIME_PUBLIC_LINK})
add_module_target(modern_all_canonical)
add_library(modern_io::modern_all_canonical ALIAS modern_all_canonical)

# ----------------------------
# 9) Executable IOApp
# ----------------------------
add_executable(IOApp ${CMAKE_CURRENT_SOURCE_DIR}/examples/io_app.cpp)
target_link_libraries(
  IOApp
  PRIVATE
    modern_io_canonical
    modern_io_async_canonical
    modern_net_canonical
    modern_net_async_canonical
    modern_net_adapters_canonical
    Threads::Threads
)

add_executable(module_compile_check EXCLUDE_FROM_ALL examples/io_app.cpp)
target_link_libraries(
  module_compile_check
  PRIVATE
    modern_io_canonical
    modern_io_async_canonical
    modern_net_canonical
    modern_net_async_canonical
    modern_net_adapters_canonical
    Threads::Threads
)

# ----------------------------
# 10) Packaging / Export
# ----------------------------
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cmake")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}Config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}Config.cmake"
  INSTALL_DESTINATION "${MODERN_IO_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}ConfigVersion.cmake"
  VERSION "${MODERN_IO_PACKAGE_VERSION}"
  COMPATIBILITY AnyNewerVersion
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}CoreTargets.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}CoreTargets.cmake"
  @ONLY
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}AsyncTargets.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}AsyncTargets.cmake"
  @ONLY
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}CanonicalTargets.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}CanonicalTargets.cmake"
  @ONLY
)

install(
  TARGETS
    modern_io net_io net_io_adapters
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  FILE_SET cxx_modules DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${MODERN_IO_PACKAGE_NAME}"
)

install(
  TARGETS
    modern_io_async net_io_async
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  FILE_SET cxx_modules DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${MODERN_IO_PACKAGE_NAME}"
)

install(
  TARGETS
    modern_all_canonical
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  FILE_SET cxx_modules DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${MODERN_IO_PACKAGE_NAME}"
)

if(TARGET modern_runtime)
  modern_io_prepare_runtime_target(${MODERN_RUNTIME_TARGET})
endif()

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}ConfigVersion.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}CoreTargets.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}AsyncTargets.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}CanonicalTargets.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modern_runtime_provider.cmake"
  DESTINATION "${MODERN_IO_INSTALL_CMAKEDIR}"
)

# ----------------------------
# 11) Google Test Integration
# ----------------------------
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# Create a minimal DartConfiguration.tcl in the build dir so ctest/CTest
# won't print "Cannot find file: ${CMAKE_BINARY_DIR}/DartConfiguration.tcl".
# This file is normally used by CDash; for local builds a minimal file is
# sufficient and avoids noisy messages from some CTest versions.
set(DART_CONFIG_IN "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DartConfiguration.tcl.in")
set(DART_CONFIG_OUT "${CMAKE_BINARY_DIR}/DartConfiguration.tcl")
if(NOT EXISTS ${DART_CONFIG_IN})
  # If the template doesn't exist in source, create a minimal template at
  # configure time inside the binary dir. This avoids requiring changes to
  # the source tree for local developers.
  file(WRITE "${DART_CONFIG_IN}" "# Minimal DartConfiguration.tcl for local builds\nset(CTEST_SITE_NAME \"Local\")\n")
endif()
configure_file(${DART_CONFIG_IN} ${DART_CONFIG_OUT} COPYONLY)

# ----------------------------
# 12) Test Targets
# ----------------------------
add_subdirectory(tests)
