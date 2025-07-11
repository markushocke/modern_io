cmake_minimum_required(VERSION 3.28)
project(IOProject LANGUAGES CXX)

# ----------------------------
# 1) Allgemeine C++20-Einstellungen
# ----------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_VERBOSE_MAKEFILE ON)  # Optional: zeigt alle Kommandos während des Builds

# ----------------------------
# 2) Compiler-Erkennung und Modul-Flags
# ----------------------------
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "✅ Clang detected: enabling module scan")
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)

elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING "⚠️ GCC detected: enabling experimental modules, manual ordering may be needed")
    add_compile_options(-fmodules-ts)

elseif (MSVC)
    message(STATUS "✅ MSVC detected: enabling module support")
    # Modul-Scan aktivieren
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
    # Wo IFC-Dateien abgelegt werden
    set(CMAKE_CXX_MODULE_DIRECTORY "${CMAKE_BINARY_DIR}/ifc")
    # Nur diesen einen Flag für Module (kein /module:interface)
    add_compile_options(/experimental:module)
    # Ab VS 17.9+ kann man optional aktivieren:
    # set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP ON)
else()
    message(WARNING "Unknown compiler: module support may not work.")
endif()

# Makro für MSVC-spezifische Target-Optionen
macro(add_module_target target)
    if (MSVC)
        set_target_properties(${target} PROPERTIES
            CXX_SCAN_FOR_MODULES YES
            CXX_MODULE_DIRECTORY "${CMAKE_CXX_MODULE_DIRECTORY}/${target}"
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
add_executable(IOApp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(IOApp PRIVATE net_io_adapters)
target_compile_features(IOApp PRIVATE cxx_std_20)
