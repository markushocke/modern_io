function(add_modern_io_package_smoke_test)
  set(options)
  set(oneValueArgs NAME SOURCE_DIR BUILD_DIR INSTALL_PREFIX)
  set(multiValueArgs BUILD_TARGETS CONFIGURE_ARGS EXPECTED_FILES)
  cmake_parse_arguments(MODERN_IO_SMOKE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(MODERN_IO_PACKAGE_SMOKE_TEST_NAME "${MODERN_IO_SMOKE_NAME}")
  set(MODERN_IO_PACKAGE_SMOKE_SOURCE_DIR "${MODERN_IO_SMOKE_SOURCE_DIR}")
  set(MODERN_IO_PACKAGE_SMOKE_BUILD_DIR "${MODERN_IO_SMOKE_BUILD_DIR}")
  set(MODERN_IO_PACKAGE_SMOKE_INSTALL_PREFIX "${MODERN_IO_SMOKE_INSTALL_PREFIX}")
  set(MODERN_IO_PACKAGE_SMOKE_PACKAGE_DIR "${MODERN_IO_PACKAGE_SMOKE_INSTALL_PREFIX}/${MODERN_IO_INSTALL_CMAKEDIR}")
  set(MODERN_IO_PACKAGE_SMOKE_BUILD_TARGETS "${MODERN_IO_SMOKE_BUILD_TARGETS}")
  set(MODERN_IO_PACKAGE_SMOKE_CONFIGURE_ARGS "${MODERN_IO_SMOKE_CONFIGURE_ARGS}")
  set(MODERN_IO_PACKAGE_SMOKE_EXPECTED_FILES "${MODERN_IO_SMOKE_EXPECTED_FILES}")

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/RunPackageSmoke.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${MODERN_IO_PACKAGE_SMOKE_TEST_NAME}.cmake"
    @ONLY
  )

  add_test(
    NAME ${MODERN_IO_PACKAGE_SMOKE_TEST_NAME}
    COMMAND "${CMAKE_COMMAND}"
      -DTEST_CONFIG=$<CONFIG>
      -P "${CMAKE_CURRENT_BINARY_DIR}/${MODERN_IO_PACKAGE_SMOKE_TEST_NAME}.cmake"
  )

  set_tests_properties(${MODERN_IO_PACKAGE_SMOKE_TEST_NAME} PROPERTIES
    LABELS "package"
  )
endfunction()

set(_modern_io_smoke_targets
  modern_io
  modern_io_async
  net_io
  net_io_async
  net_io_adapters
  modern_all_canonical
)

set(_modern_io_core_expected_files
  modern_runtime_provider.cmake
  modern_ioCoreTargets.cmake
  ../modern_trace/modern_traceConfig.cmake
  ../modern_trace/modern_traceTargets.cmake
)

set(_modern_io_async_expected_files
  modern_runtime_provider.cmake
  modern_ioCoreTargets.cmake
  modern_ioAsyncTargets.cmake
  modern_ioCanonicalTargets.cmake
  ../modern_trace/modern_traceConfig.cmake
  ../modern_trace/modern_traceTargets.cmake
  ../modern_runtime/modern_runtimeConfig.cmake
  ../modern_runtime/modern_runtimeTargets.cmake
)

set(_modern_io_async_configure_args
  "-DMODERN_RUNTIME_PROVIDER=package"
)

add_modern_io_package_smoke_test(
  NAME modern_io_package_core_smoke
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/core_consumer"
  BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/core-consumer-build"
  INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/core-install-prefix"
  BUILD_TARGETS ${_modern_io_smoke_targets}
  EXPECTED_FILES ${_modern_io_core_expected_files}
)

add_modern_io_package_smoke_test(
  NAME modern_io_package_consumer_smoke
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/consumer"
  BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/consumer-build"
  INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/install-prefix"
  BUILD_TARGETS ${_modern_io_smoke_targets}
  CONFIGURE_ARGS ${_modern_io_async_configure_args}
  EXPECTED_FILES ${_modern_io_async_expected_files}
)