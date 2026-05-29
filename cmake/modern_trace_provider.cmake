include_guard(GLOBAL)

function(modern_io_prepare_trace_target trace_target)
  if(NOT TARGET "${trace_target}")
    message(FATAL_ERROR "Trace target '${trace_target}' does not exist")
  endif()

  set(_modern_trace_property_target "${trace_target}")
  get_target_property(_modern_trace_aliased_target "${trace_target}" ALIASED_TARGET)
  if(_modern_trace_aliased_target)
    set(_modern_trace_property_target "${_modern_trace_aliased_target}")
  endif()

  get_target_property(_modern_trace_source_root "${_modern_trace_property_target}" MODERN_TRACE_SOURCE_ROOT)
  if(_modern_trace_source_root)
    return()
  endif()

  get_target_property(_modern_trace_source_dir "${_modern_trace_property_target}" SOURCE_DIR)
  if(_modern_trace_source_dir)
    set_property(TARGET "${_modern_trace_property_target}" PROPERTY MODERN_TRACE_SOURCE_ROOT "${_modern_trace_source_dir}")
  endif()
endfunction()

function(modern_io_resolve_trace_target out_target)
  set(options)
  set(oneValueArgs DEFAULT_PROVIDER LOCAL_ROOT FETCH_REPOSITORY FETCH_TAG BINARY_DIR)
  cmake_parse_arguments(MODERN_TRACE_RESOLVE "${options}" "${oneValueArgs}" "" ${ARGN})

  if(NOT DEFINED MODERN_TRACE_PROVIDER OR MODERN_TRACE_PROVIDER STREQUAL "")
    set(MODERN_TRACE_PROVIDER "${MODERN_TRACE_RESOLVE_DEFAULT_PROVIDER}")
  endif()

  set(_modern_trace_provider "${MODERN_TRACE_PROVIDER}")
  if(_modern_trace_provider STREQUAL "auto")
    if(DEFINED MODERN_TRACE_ROOT AND NOT MODERN_TRACE_ROOT STREQUAL "" AND EXISTS "${MODERN_TRACE_ROOT}/CMakeLists.txt")
      set(_modern_trace_provider "local")
    else()
      set(_modern_trace_provider "fetch")
    endif()
  endif()

  if((NOT DEFINED MODERN_TRACE_ROOT OR MODERN_TRACE_ROOT STREQUAL "")
      AND NOT MODERN_TRACE_RESOLVE_LOCAL_ROOT STREQUAL "")
    set(MODERN_TRACE_ROOT "${MODERN_TRACE_RESOLVE_LOCAL_ROOT}")
  endif()

  if((NOT DEFINED MODERN_TRACE_GIT_REPOSITORY OR MODERN_TRACE_GIT_REPOSITORY STREQUAL "")
      AND NOT MODERN_TRACE_RESOLVE_FETCH_REPOSITORY STREQUAL "")
    set(MODERN_TRACE_GIT_REPOSITORY "${MODERN_TRACE_RESOLVE_FETCH_REPOSITORY}")
  endif()

  if((NOT DEFINED MODERN_TRACE_GIT_TAG OR MODERN_TRACE_GIT_TAG STREQUAL "")
      AND NOT MODERN_TRACE_RESOLVE_FETCH_TAG STREQUAL "")
    set(MODERN_TRACE_GIT_TAG "${MODERN_TRACE_RESOLVE_FETCH_TAG}")
  endif()

  if((NOT DEFINED MODERN_TRACE_BINARY_DIR OR MODERN_TRACE_BINARY_DIR STREQUAL "")
      AND NOT MODERN_TRACE_RESOLVE_BINARY_DIR STREQUAL "")
    set(MODERN_TRACE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/modern_trace")
  endif()

  if(TARGET modern_trace::modern_trace)
    modern_io_prepare_trace_target(modern_trace::modern_trace)
    set(${out_target} modern_trace::modern_trace PARENT_SCOPE)
    return()
  endif()

  if(_modern_trace_provider STREQUAL "package")
    find_package(modern_trace CONFIG REQUIRED)
  elseif(_modern_trace_provider STREQUAL "local")
    if(NOT DEFINED MODERN_TRACE_ROOT OR MODERN_TRACE_ROOT STREQUAL "")
      message(FATAL_ERROR "MODERN_TRACE_PROVIDER=local requires MODERN_TRACE_ROOT to point to a modern_trace checkout")
    endif()

    if(NOT EXISTS "${MODERN_TRACE_ROOT}/CMakeLists.txt")
      message(FATAL_ERROR "modern_trace was not found at MODERN_TRACE_ROOT=${MODERN_TRACE_ROOT}")
    endif()

    if(NOT DEFINED MODERN_TRACE_BINARY_DIR OR MODERN_TRACE_BINARY_DIR STREQUAL "")
      set(MODERN_TRACE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/modern_trace")
    endif()

    if(NOT TARGET modern_trace AND NOT TARGET modern_trace::modern_trace)
      add_subdirectory("${MODERN_TRACE_ROOT}" "${MODERN_TRACE_BINARY_DIR}")
    endif()
  elseif(_modern_trace_provider STREQUAL "fetch")
    if(NOT DEFINED MODERN_TRACE_GIT_REPOSITORY OR MODERN_TRACE_GIT_REPOSITORY STREQUAL "")
      message(FATAL_ERROR "MODERN_TRACE_PROVIDER=fetch requires MODERN_TRACE_GIT_REPOSITORY")
    endif()

    if(NOT DEFINED MODERN_TRACE_GIT_TAG OR MODERN_TRACE_GIT_TAG STREQUAL "")
      message(FATAL_ERROR "MODERN_TRACE_PROVIDER=fetch requires MODERN_TRACE_GIT_TAG")
    endif()

    include(FetchContent)
    if(NOT TARGET modern_trace AND NOT TARGET modern_trace::modern_trace)
      FetchContent_Declare(
        modern_trace
        GIT_REPOSITORY "${MODERN_TRACE_GIT_REPOSITORY}"
        GIT_TAG "${MODERN_TRACE_GIT_TAG}"
        GIT_SHALLOW TRUE
      )
      FetchContent_MakeAvailable(modern_trace)
    endif()
  else()
    message(FATAL_ERROR "Unsupported MODERN_TRACE_PROVIDER=${MODERN_TRACE_PROVIDER}. Expected one of: auto, package, local, fetch")
  endif()

  if(TARGET modern_trace AND NOT TARGET modern_trace::modern_trace)
    add_library(modern_trace::modern_trace ALIAS modern_trace)
  endif()

  if(TARGET modern_trace::modern_trace)
    set(_modern_trace_target modern_trace::modern_trace)
  elseif(TARGET modern_trace)
    set(_modern_trace_target modern_trace)
  else()
    message(FATAL_ERROR "modern_trace provider '${_modern_trace_provider}' did not create target modern_trace::modern_trace or modern_trace")
  endif()

  modern_io_prepare_trace_target("${_modern_trace_target}")

  set(${out_target} "${_modern_trace_target}" PARENT_SCOPE)
endfunction()