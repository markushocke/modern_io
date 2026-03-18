cmake_minimum_required(VERSION 3.28)
project(IOProject VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(MODERN_IO_PACKAGE_NAME modern_io)
set(MODERN_IO_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${MODERN_IO_PACKAGE_NAME}")
set(MODERN_IO_PACKAGE_VERSION "${PROJECT_VERSION}")

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
    #-stdlib=libc++            # ENTFERNT: Nur aktivieren wenn libc++ installiert
        -Wno-unused-command-line-argument
        -Wno-unknown-warning-option
        -Wno-missing-braces
        # Clang 18 reports __cpp_concepts=201907L (old draft) in C++ module mode,
        # which causes GCC's <expected> header guard to fail. Override with the
        # correct C++20 final value so <expected> is visible in global module fragments.
        -D__cpp_concepts=202002L
        -Wno-builtin-macro-redefined
    )
    # add_link_options(-stdlib=libc++)  # ENTFERNT
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING "⚠️ GCC detected: enabling experimental modules, manual ordering may be needed")
    add_compile_options(-fmodules-ts)
endif()

# Always explicitly find/link threads
find_package(Threads REQUIRED)

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
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_exceptions.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_concepts.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_file.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_data.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_buffered.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_iostream.ixx
)
target_compile_features(modern_io PUBLIC cxx_std_23)
target_link_libraries(modern_io PUBLIC Threads::Threads)
add_module_target(modern_io)
add_library(modern_io::modern_io ALIAS modern_io)

# ----------------------------
# 4) modern_io_async Library
# ----------------------------
add_library(modern_io_async)
target_sources(modern_io_async
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_async.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_task.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_async_data.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_async_buffered.ixx
)
target_compile_features(modern_io_async PUBLIC cxx_std_23)
target_link_libraries(modern_io_async PUBLIC modern_io Threads::Threads)
add_module_target(modern_io_async)
add_library(modern_io::modern_io_async ALIAS modern_io_async)

# ----------------------------
# 5) net_io Library (sync)
# ----------------------------
add_library(net_io)
target_sources(net_io
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_exceptions.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_base.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_concepts.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_tcp_endpoint.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_tcp_client.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_tcp_server.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_udp_endpoint.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_udp_transport.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_ipc_transport.ixx
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
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_async.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_task.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_async_concepts.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_event_loop.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_generic_awaiter.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_async_tcp_socket.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_async_udp_socket.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_async_stream_base.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_async_stream_adapters.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_async_utils.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_async_tcp_server.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_scheduler.ixx
)
    target_link_libraries(net_io_async PUBLIC net_io modern_io_async Threads::Threads)
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
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_adapters.ixx
)
target_link_libraries(net_io_adapters PUBLIC modern_io net_io Threads::Threads)
target_compile_features(net_io_adapters PUBLIC cxx_std_23)
add_module_target(net_io_adapters)
add_library(modern_io::net_io_adapters ALIAS net_io_adapters)

# ----------------------------
# 8) Executable IOApp
# ----------------------------
add_executable(IOApp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(IOApp PRIVATE net_io_adapters modern_io_async net_io_async Threads::Threads)

add_executable(module_compile_check EXCLUDE_FROM_ALL main.cpp)
target_link_libraries(module_compile_check PRIVATE net_io_adapters modern_io_async net_io_async Threads::Threads)

# ----------------------------
# 9) Packaging / Export
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

export(
  TARGETS modern_io modern_io_async net_io net_io_async net_io_adapters
  FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}Targets.cmake"
  NAMESPACE ${MODERN_IO_PACKAGE_NAME}::
)

install(
  TARGETS modern_io modern_io_async net_io net_io_async net_io_adapters
  EXPORT ${MODERN_IO_PACKAGE_NAME}Targets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  FILE_SET cxx_modules DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${MODERN_IO_PACKAGE_NAME}"
)

install(
  EXPORT ${MODERN_IO_PACKAGE_NAME}Targets
  FILE ${MODERN_IO_PACKAGE_NAME}Targets.cmake
  NAMESPACE ${MODERN_IO_PACKAGE_NAME}::
  DESTINATION "${MODERN_IO_INSTALL_CMAKEDIR}"
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/${MODERN_IO_PACKAGE_NAME}ConfigVersion.cmake"
  DESTINATION "${MODERN_IO_INSTALL_CMAKEDIR}"
)

# ----------------------------
# 10) Google Test Integration
# ----------------------------
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
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
# 11) Test Targets
# ----------------------------
add_subdirectory(tests)