# Tests CMakeLists.txt

# Common test settings
set(TEST_COMPILE_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
)

# Add test subdirectories
add_subdirectory(modern_io)
add_subdirectory(net_io)
add_subdirectory(net_io_adapters)
add_subdirectory(package_smoke)
