include_guard(GLOBAL)

function(modern_io_prepare_runtime_target runtime_target)
  if(NOT TARGET "${runtime_target}")
    message(FATAL_ERROR "Runtime target '${runtime_target}' does not exist")
  endif()

  set(_modern_runtime_property_target "${runtime_target}")
  get_target_property(_modern_runtime_aliased_target "${runtime_target}" ALIASED_TARGET)
  if(_modern_runtime_aliased_target)
    set(_modern_runtime_property_target "${_modern_runtime_aliased_target}")
  endif()

  get_target_property(_modern_runtime_source_root "${_modern_runtime_property_target}" MODERN_RUNTIME_SOURCE_ROOT)
  if(_modern_runtime_source_root)
    return()
  endif()

  get_target_property(_modern_runtime_source_dir "${_modern_runtime_property_target}" SOURCE_DIR)
  if(_modern_runtime_source_dir)
    set_property(TARGET "${_modern_runtime_property_target}" PROPERTY MODERN_RUNTIME_SOURCE_ROOT "${_modern_runtime_source_dir}")
  endif()
endfunction()

function(modern_io_resolve_runtime_target out_target)
  set(options)
  set(oneValueArgs DEFAULT_PROVIDER LOCAL_ROOT FETCH_REPOSITORY FETCH_TAG SOURCE_DIR BINARY_DIR)
  cmake_parse_arguments(MODERN_RUNTIME_RESOLVE "${options}" "${oneValueArgs}" "" ${ARGN})

  if(NOT DEFINED MODERN_RUNTIME_PROVIDER OR MODERN_RUNTIME_PROVIDER STREQUAL "")
    set(MODERN_RUNTIME_PROVIDER "${MODERN_RUNTIME_RESOLVE_DEFAULT_PROVIDER}")
  endif()

  if((NOT DEFINED MODERN_RUNTIME_ROOT OR MODERN_RUNTIME_ROOT STREQUAL "")
      AND NOT MODERN_RUNTIME_RESOLVE_LOCAL_ROOT STREQUAL "")
    set(MODERN_RUNTIME_ROOT "${MODERN_RUNTIME_RESOLVE_LOCAL_ROOT}")
  endif()

  set(_modern_runtime_provider "${MODERN_RUNTIME_PROVIDER}")
  if(_modern_runtime_provider STREQUAL "auto")
    if(DEFINED MODERN_RUNTIME_ROOT AND NOT MODERN_RUNTIME_ROOT STREQUAL "" AND EXISTS "${MODERN_RUNTIME_ROOT}/CMakeLists.txt")
      set(_modern_runtime_provider "local")
    else()
      set(_modern_runtime_provider "fetch")
    endif()
  endif()

  if((NOT DEFINED MODERN_RUNTIME_GIT_REPOSITORY OR MODERN_RUNTIME_GIT_REPOSITORY STREQUAL "")
      AND NOT MODERN_RUNTIME_RESOLVE_FETCH_REPOSITORY STREQUAL "")
    set(MODERN_RUNTIME_GIT_REPOSITORY "${MODERN_RUNTIME_RESOLVE_FETCH_REPOSITORY}")
  endif()

  if((NOT DEFINED MODERN_RUNTIME_GIT_TAG OR MODERN_RUNTIME_GIT_TAG STREQUAL "")
      AND NOT MODERN_RUNTIME_RESOLVE_FETCH_TAG STREQUAL "")
    set(MODERN_RUNTIME_GIT_TAG "${MODERN_RUNTIME_RESOLVE_FETCH_TAG}")
  endif()

  if((NOT DEFINED MODERN_RUNTIME_BINARY_DIR OR MODERN_RUNTIME_BINARY_DIR STREQUAL "")
      AND NOT MODERN_RUNTIME_RESOLVE_BINARY_DIR STREQUAL "")
    set(MODERN_RUNTIME_BINARY_DIR "${MODERN_RUNTIME_RESOLVE_BINARY_DIR}")
  endif()

  if((NOT DEFINED MODERN_RUNTIME_SOURCE_DIR OR MODERN_RUNTIME_SOURCE_DIR STREQUAL "")
      AND NOT MODERN_RUNTIME_RESOLVE_SOURCE_DIR STREQUAL "")
    set(MODERN_RUNTIME_SOURCE_DIR "${MODERN_RUNTIME_RESOLVE_SOURCE_DIR}")
  endif()

  if(TARGET modern_runtime::modern_runtime)
    modern_io_prepare_runtime_target(modern_runtime::modern_runtime)
    set(${out_target} modern_runtime::modern_runtime PARENT_SCOPE)
    return()
  endif()

  if(_modern_runtime_provider STREQUAL "package")
    find_package(modern_runtime CONFIG REQUIRED)
  elseif(_modern_runtime_provider STREQUAL "local")
    if(NOT DEFINED MODERN_RUNTIME_ROOT OR MODERN_RUNTIME_ROOT STREQUAL "")
      message(FATAL_ERROR "MODERN_RUNTIME_PROVIDER=local requires MODERN_RUNTIME_ROOT to point to a modern_runtime checkout")
    endif()

    if(NOT EXISTS "${MODERN_RUNTIME_ROOT}/CMakeLists.txt")
      message(FATAL_ERROR "modern_runtime was not found at MODERN_RUNTIME_ROOT=${MODERN_RUNTIME_ROOT}")
    endif()

    if(NOT DEFINED MODERN_RUNTIME_BINARY_DIR OR MODERN_RUNTIME_BINARY_DIR STREQUAL "")
      set(MODERN_RUNTIME_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/modern_runtime-build")
    endif()

    if(NOT TARGET modern_runtime AND NOT TARGET modern_runtime::modern_runtime)
      add_subdirectory("${MODERN_RUNTIME_ROOT}" "${MODERN_RUNTIME_BINARY_DIR}")
    endif()
  elseif(_modern_runtime_provider STREQUAL "fetch")
    if(NOT DEFINED MODERN_RUNTIME_GIT_REPOSITORY OR MODERN_RUNTIME_GIT_REPOSITORY STREQUAL "")
      message(FATAL_ERROR "MODERN_RUNTIME_PROVIDER=fetch requires MODERN_RUNTIME_GIT_REPOSITORY")
    endif()

    if(NOT DEFINED MODERN_RUNTIME_GIT_TAG OR MODERN_RUNTIME_GIT_TAG STREQUAL "")
      message(FATAL_ERROR "MODERN_RUNTIME_PROVIDER=fetch requires MODERN_RUNTIME_GIT_TAG")
    endif()

    include(FetchContent)
    if(NOT TARGET modern_runtime AND NOT TARGET modern_runtime::modern_runtime)
      set(_modern_runtime_fetch_content_args)
      if(DEFINED MODERN_RUNTIME_SOURCE_DIR AND NOT MODERN_RUNTIME_SOURCE_DIR STREQUAL "")
        list(APPEND _modern_runtime_fetch_content_args SOURCE_DIR "${MODERN_RUNTIME_SOURCE_DIR}")
      endif()
      if(DEFINED MODERN_RUNTIME_BINARY_DIR AND NOT MODERN_RUNTIME_BINARY_DIR STREQUAL "")
        list(APPEND _modern_runtime_fetch_content_args BINARY_DIR "${MODERN_RUNTIME_BINARY_DIR}")
      endif()
      FetchContent_Declare(
        modern_runtime
        GIT_REPOSITORY "${MODERN_RUNTIME_GIT_REPOSITORY}"
        GIT_TAG "${MODERN_RUNTIME_GIT_TAG}"
        GIT_SHALLOW TRUE
        ${_modern_runtime_fetch_content_args}
      )
      FetchContent_MakeAvailable(modern_runtime)
    endif()
  else()
    message(FATAL_ERROR "Unsupported MODERN_RUNTIME_PROVIDER=${MODERN_RUNTIME_PROVIDER}. Expected one of: auto, package, local, fetch")
  endif()

  if(TARGET modern_runtime AND NOT TARGET modern_runtime::modern_runtime)
    add_library(modern_runtime::modern_runtime ALIAS modern_runtime)
  endif()

  if(TARGET modern_runtime::modern_runtime)
    set(_modern_runtime_target modern_runtime::modern_runtime)
  elseif(TARGET modern_runtime)
    set(_modern_runtime_target modern_runtime)
  else()
    message(FATAL_ERROR "modern_runtime provider '${_modern_runtime_provider}' did not create target modern_runtime::modern_runtime or modern_runtime")
  endif()

  modern_io_prepare_runtime_target("${_modern_runtime_target}")

  set(${out_target} "${_modern_runtime_target}" PARENT_SCOPE)
endfunction()
