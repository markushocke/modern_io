cmake_minimum_required(VERSION 3.28)
project(modern_io_package_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(modern_io 0.1 CONFIG REQUIRED)

foreach(required_target IN ITEMS
  modern_io::modern_io
  modern_io::modern_io_async
  modern_io::net_io
  modern_io::net_io_async
  modern_io::net_io_adapters
)
  if(NOT TARGET "${required_target}")
    message(FATAL_ERROR "Required target missing from package: ${required_target}")
  endif()
endforeach()

add_executable(modern_io_package_smoke main.cpp)
target_link_libraries(modern_io_package_smoke PRIVATE
  modern_io::modern_io
  modern_io::modern_io_async
  modern_io::net_io
  modern_io::net_io_async
  modern_io::net_io_adapters
)
target_compile_features(modern_io_package_smoke PRIVATE cxx_std_23)