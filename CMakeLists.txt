cmake_minimum_required(VERSION 3.28)
project(IOProject LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler detection and options
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "✅ Clang detected: enabling modules and scan support")
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
    #add_compile_options(-fmodules-ts)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING "⚠️ GCC detected: modules require manual compilation order (no scan support)")
    add_compile_options(-fmodules-ts)
elseif(MSVC)
    message(STATUS "✅ MSVC detected: enabling module support")
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
    add_compile_options(/experimental:module)
else()
    message(WARNING "Unknown compiler: module support may not work.")
endif()

# Libraries with modules (unchanged, but benefits from scan)
add_library(modern_io)
target_sources(modern_io
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_concepts.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_file.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_data.ixx
      ${CMAKE_CURRENT_SOURCE_DIR}/modern_io_buffered.ixx
)
target_compile_features(modern_io PUBLIC cxx_std_20)

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
target_compile_features(net_io PUBLIC cxx_std_20)

add_library(net_io_adapters)
target_sources(net_io_adapters
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/net_io_adapters.ixx
)
target_link_libraries(net_io_adapters
  PUBLIC modern_io net_io
)
target_compile_features(net_io_adapters PUBLIC cxx_std_20)

# Executable
add_executable(IOApp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(IOApp PRIVATE net_io_adapters)
target_compile_features(IOApp PRIVATE cxx_std_20)
