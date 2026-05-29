# modern_io tests

# Test for concepts
add_executable(test_modern_io_concepts test_concepts.cpp)
target_link_libraries(test_modern_io_concepts PRIVATE modern_io GTest::gtest_main)
target_compile_features(test_modern_io_concepts PRIVATE cxx_std_23)
gtest_discover_tests(test_modern_io_concepts)

# Test for file
add_executable(test_modern_io_file test_file.cpp)
target_link_libraries(test_modern_io_file PRIVATE modern_io GTest::gtest_main)
target_compile_features(test_modern_io_file PRIVATE cxx_std_23)
gtest_discover_tests(test_modern_io_file)

# Test for data
add_executable(test_modern_io_data test_data.cpp)
target_link_libraries(test_modern_io_data PRIVATE modern_io GTest::gtest_main)
target_compile_features(test_modern_io_data PRIVATE cxx_std_23)
gtest_discover_tests(test_modern_io_data)

# Test for buffered
add_executable(test_modern_io_buffered test_buffered.cpp)
target_link_libraries(test_modern_io_buffered PRIVATE modern_io modern_io_async GTest::gtest_main)
target_compile_features(test_modern_io_buffered PRIVATE cxx_std_23)
gtest_discover_tests(test_modern_io_buffered)

# Test for trace
add_executable(test_modern_io_trace test_trace.cpp)
target_link_libraries(test_modern_io_trace PRIVATE modern_io GTest::gtest_main)
target_compile_features(test_modern_io_trace PRIVATE cxx_std_23)
gtest_discover_tests(test_modern_io_trace)

# Test for task
add_executable(test_modern_io_task test_task.cpp)
target_link_libraries(test_modern_io_task PRIVATE modern_io_async GTest::gtest_main)
target_compile_features(test_modern_io_task PRIVATE cxx_std_23)
gtest_discover_tests(test_modern_io_task)

# Test for iostream
add_executable(test_modern_io_iostream test_iostream.cpp)
target_link_libraries(test_modern_io_iostream PRIVATE modern_io GTest::gtest_main)
target_compile_features(test_modern_io_iostream PRIVATE cxx_std_23)
gtest_discover_tests(test_modern_io_iostream)

# Test for canonical modern.* imports
add_executable(test_modern_io_canonical_imports test_canonical_imports.cpp)
target_link_libraries(
	test_modern_io_canonical_imports
	PRIVATE
		modern_io_canonical
		modern_io_async_canonical
		modern_net_canonical
		modern_net_async_canonical
		modern_net_adapters_canonical
		GTest::gtest_main
)
target_compile_features(test_modern_io_canonical_imports PRIVATE cxx_std_23)
gtest_discover_tests(test_modern_io_canonical_imports)
