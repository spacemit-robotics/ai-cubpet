# CycloneDDS + CycloneDDS-CXX bootstrap for ai-cubpet DDS support.
#
# idlcxx_generate() is a configure-time CMake macro, so CycloneDDS-CXX must be
# built and installed before the parent CMakeLists.txt calls find_package().

if(DEFINED _AI_CUBPET_CYCLONEDDS_LOADED)
  return()
endif()
set(_AI_CUBPET_CYCLONEDDS_LOADED ON)

get_filename_component(_AI_CUBPET_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${_AI_CUBPET_ROOT}/cmake/FetchThirdParty.cmake")

if(DEFINED ENV{SROBOTIS_THIRDPARTY_CACHE})
  set(_AI_CUBPET_TP_CACHE "$ENV{SROBOTIS_THIRDPARTY_CACHE}")
elseif(DEFINED ENV{HOME})
  set(_AI_CUBPET_TP_CACHE "$ENV{HOME}/.cache/thirdparty")
else()
  set(_AI_CUBPET_TP_CACHE "${CMAKE_BINARY_DIR}/.cache/thirdparty")
endif()

set(AI_CUBPET_CYCLONEDDS_INSTALL_PREFIX
    "${CMAKE_INSTALL_PREFIX}"
    CACHE PATH "Install prefix for ai-cubpet-built CycloneDDS")
option(AI_CUBPET_FORCE_CYCLONEDDS_REBUILD
       "Force rebuild of ai-cubpet CycloneDDS dependencies" OFF)

set(_CYCLONEDDS_GIT_REPO "https://gitee.com/spacemit-robotics/cyclonedds.git")
set(_CYCLONEDDS_CXX_GIT_REPO "https://gitee.com/spacemit-robotics/cyclonedds-cxx.git")
set(_CYCLONEDDS_GIT_COMMIT "c7b1e48a5bdbcedf16e7500594f335905ed76fbb")
set(_CYCLONEDDS_CXX_GIT_COMMIT "20ccaa519ca09a7b1776b8630389aa19ce8da61a")

if(DEFINED ENV{SROBOTIS_CYCLONEDDS_GIT_REPO})
  set(_CYCLONEDDS_GIT_REPO "$ENV{SROBOTIS_CYCLONEDDS_GIT_REPO}")
endif()
if(DEFINED ENV{SROBOTIS_CYCLONEDDS_CXX_GIT_REPO})
  set(_CYCLONEDDS_CXX_GIT_REPO "$ENV{SROBOTIS_CYCLONEDDS_CXX_GIT_REPO}")
endif()
if(DEFINED ENV{SROBOTIS_CYCLONEDDS_GIT_COMMIT})
  set(_CYCLONEDDS_GIT_COMMIT "$ENV{SROBOTIS_CYCLONEDDS_GIT_COMMIT}")
endif()
if(DEFINED ENV{SROBOTIS_CYCLONEDDS_CXX_GIT_COMMIT})
  set(_CYCLONEDDS_CXX_GIT_COMMIT "$ENV{SROBOTIS_CYCLONEDDS_CXX_GIT_COMMIT}")
endif()

fetch_thirdparty(NAME cyclonedds
  GIT_REPO "${_CYCLONEDDS_GIT_REPO}"
  GIT_COMMIT "${_CYCLONEDDS_GIT_COMMIT}"
  OUT_SOURCE_DIR _CYCLONEDDS_SOURCE_DIR)

fetch_thirdparty(NAME cyclonedds-cxx
  GIT_REPO "${_CYCLONEDDS_CXX_GIT_REPO}"
  GIT_COMMIT "${_CYCLONEDDS_CXX_GIT_COMMIT}"
  OUT_SOURCE_DIR _CYCLONEDDS_CXX_SOURCE_DIR)

set(_CYCLONEDDS_BUILD_DIR "${_AI_CUBPET_TP_CACHE}/cyclonedds/build")
set(_CYCLONEDDS_CXX_BUILD_DIR "${_AI_CUBPET_TP_CACHE}/cyclonedds-cxx/build")
set(_CYCLONEDDS_CONFIG
    "${AI_CUBPET_CYCLONEDDS_INSTALL_PREFIX}/lib/cmake/CycloneDDS/CycloneDDSConfig.cmake")
set(_CYCLONEDDS_CXX_CONFIG
    "${AI_CUBPET_CYCLONEDDS_INSTALL_PREFIX}/lib/cmake/CycloneDDS-CXX/CycloneDDS-CXXConfig.cmake")

if(DEFINED ENV{PARALLEL_JOBS} AND NOT "$ENV{PARALLEL_JOBS}" STREQUAL "")
  set(_CYCLONEDDS_BUILD_JOBS "$ENV{PARALLEL_JOBS}")
elseif(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL} AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
  set(_CYCLONEDDS_BUILD_JOBS "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
else()
  set(_CYCLONEDDS_BUILD_JOBS "4")
endif()

function(_ai_cubpet_run_cyclonedds_step desc)
  message(STATUS "${desc}")
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE _res)
  if(NOT _res EQUAL 0)
    message(FATAL_ERROR "${desc} failed with exit code ${_res}")
  endif()
endfunction()

if(AI_CUBPET_FORCE_CYCLONEDDS_REBUILD OR NOT EXISTS "${_CYCLONEDDS_CONFIG}")
  _ai_cubpet_run_cyclonedds_step("Configuring CycloneDDS"
    "${CMAKE_COMMAND}" -S "${_CYCLONEDDS_SOURCE_DIR}" -B "${_CYCLONEDDS_BUILD_DIR}"
      "-DCMAKE_BUILD_TYPE=Release"
      "-DCMAKE_INSTALL_PREFIX=${AI_CUBPET_CYCLONEDDS_INSTALL_PREFIX}"
      "-DBUILD_EXAMPLES=OFF"
      "-DBUILD_TESTING=OFF")
  _ai_cubpet_run_cyclonedds_step("Building CycloneDDS"
    "${CMAKE_COMMAND}" --build "${_CYCLONEDDS_BUILD_DIR}" --parallel "${_CYCLONEDDS_BUILD_JOBS}")
  _ai_cubpet_run_cyclonedds_step("Installing CycloneDDS"
    "${CMAKE_COMMAND}" --install "${_CYCLONEDDS_BUILD_DIR}")
endif()

if(AI_CUBPET_FORCE_CYCLONEDDS_REBUILD OR NOT EXISTS "${_CYCLONEDDS_CXX_CONFIG}")
  _ai_cubpet_run_cyclonedds_step("Configuring CycloneDDS-CXX"
    "${CMAKE_COMMAND}" -S "${_CYCLONEDDS_CXX_SOURCE_DIR}" -B "${_CYCLONEDDS_CXX_BUILD_DIR}"
      "-DCMAKE_BUILD_TYPE=Release"
      "-DCMAKE_INSTALL_PREFIX=${AI_CUBPET_CYCLONEDDS_INSTALL_PREFIX}"
      "-DCMAKE_PREFIX_PATH=${AI_CUBPET_CYCLONEDDS_INSTALL_PREFIX}"
      "-DBUILD_EXAMPLES=OFF"
      "-DBUILD_TESTING=OFF")
  _ai_cubpet_run_cyclonedds_step("Building CycloneDDS-CXX"
    "${CMAKE_COMMAND}" --build "${_CYCLONEDDS_CXX_BUILD_DIR}" --parallel "${_CYCLONEDDS_BUILD_JOBS}")
  _ai_cubpet_run_cyclonedds_step("Installing CycloneDDS-CXX"
    "${CMAKE_COMMAND}" --install "${_CYCLONEDDS_CXX_BUILD_DIR}")
endif()

list(PREPEND CMAKE_PREFIX_PATH "${AI_CUBPET_CYCLONEDDS_INSTALL_PREFIX}")
set(CycloneDDS_DIR
    "${AI_CUBPET_CYCLONEDDS_INSTALL_PREFIX}/lib/cmake/CycloneDDS"
    CACHE PATH "SDK-built CycloneDDS CMake package" FORCE)
set(CycloneDDS-CXX_DIR
    "${AI_CUBPET_CYCLONEDDS_INSTALL_PREFIX}/lib/cmake/CycloneDDS-CXX"
    CACHE PATH "SDK-built CycloneDDS-CXX CMake package" FORCE)
