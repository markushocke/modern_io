cmake_minimum_required(VERSION 3.28)
project(IOProject LANGUAGES CXX)

# ----------------------------
# 1) General C++20 settings
# ----------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_VERBOSE_MAKEFILE ON)  # Optional: shows all commands during the build process for debugging purposes

# ----------------------------
# 2) Compiler detection and module flags
# ----------------------------
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "✅ Clang detected: enabling module scan")
    set(CMAKE_CXX_SCAN_FOR_MODULES ON) # Enables automatic scanning for C++20 modules with Clang

elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING "⚠️ GCC detected: enabling experimental modules, manual ordering may be needed")
    add_compile_options(-fmodules-ts) # Enables experimental C++20 modules support for GCC

elseif (MSVC)
    message(STATUS "✅ MSVC detected: enabling module support")
    # Enable module scanning for MSVC (Visual Studio 2022 17.6+)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
    # Specify the directory where interface files (IFC) are stored
    set(CMAKE_CXX_MODULE_DIRECTORY "${CMAKE_BINARY_DIR}/ifc")
    # Use only this flag for modules (do not use /module:interface directly)
    add_compile_options(/experimental:module)
    # For Visual Studio 17.9+ you can optionally enable dynamic dependency scanning:
    # set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP ON)
else()
    message(WARNING "Unknown compiler: module support may not work. Please check your toolchain documentation.")
endif()

# Macro for setting MSVC-specific target properties for modules
macro(add_module_target target)
    if (MSVC)
        set_target_properties(${target} PROPERTIES
            CXX_SCAN_FOR_MODULES YES # Enables module scanning for this target
            CXX_MODULE_DIRECTORY "${CMAKE_CXX_MODULE_DIRECTORY}/${target}" # Sets a separate IFC directory per target
        )
    endif()
endmacro()

# ----------------------------
# 3) modern_io Library
# ----------------------------
add_library(modern_io)
target_sources(modern_io
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_concepts.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_file.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_data.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_buffered.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_iostream.ixx
)
target_compile_features(modern_io PUBLIC cxx_std_20)
add_module_target(modern_io)

# ----------------------------
# 4) net_io Library
# ----------------------------
# This library provides network primitives such as TcpClient, TcpServer, UdpTransport, etc.
add_library(net_io)
target_sources(net_io
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_base.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_concepts.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/tcp_endpoint.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/tcp_client.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/tcp_server.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/udp_endpoint.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/udp_transport.ixx
)
target_link_libraries(net_io PUBLIC modern_io)
target_compile_features(net_io PUBLIC cxx_std_20)
add_module_target(net_io)

# ----------------------------
# 5) net_io_adapters Library
# ----------------------------
# This library provides adapters to bridge modern_io and net_io, such as TransportSink, TransportSource, etc.
add_library(net_io_adapters)
target_sources(net_io_adapters
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_adapters.ixx
)
target_link_libraries(net_io_adapters PUBLIC modern_io net_io)
target_compile_features(net_io_adapters PUBLIC cxx_std_20)
add_module_target(net_io_adapters)

# ----------------------------
# 6) Executable IOApp
# ----------------------------
# This is the main application that uses the net_io_adapters library.
add_executable(IOApp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(IOApp PRIVATE net_io_adapters)
target_compile_features(IOApp PRIVATE cxx_std_20)

# Compile check: Dummy target to ensure all modules compile together.
# This target is not built by default, but can be used to check module integration.
add_executable(module_compile_check EXCLUDE_FROM_ALL main.cpp)
target_link_libraries(module_compile_check PRIVATE net_io_adapters)
target_compile_features(module_compile_check PRIVATE cxx_std_20)
