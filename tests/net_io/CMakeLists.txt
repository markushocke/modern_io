# net_io tests

# helper functions to add test executables with project root added to include dirs
function(add_net_sync_test name src)
	add_executable(${name} ${src})
	target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})
	target_link_libraries(${name} PRIVATE net_io GTest::gtest_main)
	target_compile_features(${name} PRIVATE cxx_std_23)
	gtest_discover_tests(${name})
endfunction()

function(add_net_async_test name src)
	add_executable(${name} ${src})
	target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})
	target_link_libraries(${name} PRIVATE net_io_async GTest::gtest_main)
	target_compile_features(${name} PRIVATE cxx_std_23)
	gtest_discover_tests(${name})
endfunction()

# Test for tcp_endpoint
add_net_sync_test(test_net_io_tcp_endpoint "test_tcp_endpoint.cpp")

# Test for tcp_client
add_net_sync_test(test_net_io_tcp_client "test_tcp_client.cpp")

# Test for tcp_server
add_net_sync_test(test_net_io_tcp_server "test_tcp_server.cpp")

# Test for udp_transport
add_net_sync_test(test_net_io_udp_transport "test_udp_transport.cpp")

# Integration test: UDP ping/pong
add_net_sync_test(test_net_io_udp_ping_pong "udp_ping_pong_integration.cpp")

# Test for base
add_net_sync_test(test_net_io_base "test_base.cpp")

# Compile-time concept/API static checks
add_net_async_test(test_net_io_concepts_static "test_concepts_static.cpp")

# Per-class concept checks
add_net_sync_test(test_net_io_concepts_udp "test_concepts_udp.cpp")

add_net_async_test(test_net_io_concepts_async "test_concepts_async.cpp")

# Async TCP runtime tests
add_net_async_test(test_net_io_async_tcp "test_async_tcp.cpp")

# Async UDP runtime test
add_net_async_test(test_net_io_async_udp "test_async_udp.cpp")

# Async UDP multi-client test
add_net_async_test(test_net_io_async_udp_multi "test_async_udp_multi.cpp")

# Async TCP multi-client test
add_net_async_test(test_net_io_async_tcp_multi "test_async_tcp_multi.cpp")

# EventLoop unit tests
add_net_async_test(test_net_io_event_loop "test_event_loop.cpp")

# IoTask unit test
add_net_async_test(test_net_io_io_task "test_io_task.cpp")

# Runtime bridge conformance tests (io_bridge / sender_bridge)
add_net_async_test(test_net_io_runtime_bridges "test_runtime_bridges.cpp")

# Runtime Task fluent API conformance tests
add_net_async_test(test_net_io_runtime_task_fluent "test_runtime_task_fluent.cpp")
