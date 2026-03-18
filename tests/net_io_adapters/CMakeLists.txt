# net_io_adapters tests

# Test for adapters
add_executable(test_net_io_adapters test_adapters.cpp)
target_link_libraries(test_net_io_adapters PRIVATE net_io_adapters GTest::gtest_main)
target_compile_features(test_net_io_adapters PRIVATE cxx_std_23)
gtest_discover_tests(test_net_io_adapters)
