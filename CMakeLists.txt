cmake_minimum_required(VERSION 3.20)

# ============================================================================
# vcpkg integration (must come before project())
#
# Same approach as the reference project (Cubium): if the environment provides
# VCPKG_ROOT, automatically wire up the vcpkg toolchain so that arrow /
# nlohmann-json are auto-installed in manifest mode.
# Note: on Windows the VS development environment must be injected first via
# VsDevCmd.bat so that cl.exe is on PATH; otherwise vcpkg cannot locate the VS
# instance (the VS 18 Preview on this machine is not recognized by vswhere).
# ============================================================================
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
elseif(EXISTS "E:/vcpkg/scripts/buildsystems/vcpkg.cmake" AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "E:/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# ============================================================================
# trace-replay — replays the event stream of a rawproc-sorted Parquet trace in
# time-series order.
#
# Core design (aligned with the rawproc division of labor):
#   * The input is rawproc's events_tsorted/bucket=xxxxxx: globally ordered
#     across buckets, ordered by (machine_ts, log_offset) within a bucket, so a
#     sequential merge yields the global time order.
#   * replay is fd-first rather than path-first: IO operations trust only
#     (pid, fd), mapped through a per-pid fd table to the fd actually opened by
#     this process, on which the syscall is executed (openat-replay).
#   * Real syscalls are executed under a restricted sandbox root, with path
#     traversal protection.
# ============================================================================

project(trace_replay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release; trace data is large and performance matters.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(TR_TRACE_REPLAY_BUILD_TESTS "Build trace-replay unit tests" OFF)

# Compile warning configuration (shared)
add_library(TraceReplayWarnings INTERFACE)
target_include_directories(TraceReplayWarnings INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
include(cmake/CompilerWarnings.cmake)
set_project_warnings(TraceReplayWarnings)

# ----------------------------------------------------------------------------
# Third-party dependencies
# ----------------------------------------------------------------------------

# Parsing rawproc's Parquet output depends on Apache Arrow.
find_package(Arrow CONFIG REQUIRED)
find_package(Parquet CONFIG REQUIRED)

# JSON config parsing uses nlohmann/json (header-only).
find_package(nlohmann_json CONFIG REQUIRED)

# Choose the Arrow link target: prefer the shared library (vcpkg x64-windows is
# dynamic by default).
set(_ARROW_TARGET "$<IF:$<BOOL:${ARROW_BUILD_STATIC}>,Arrow::arrow_static,Arrow::arrow_shared>")
set(_PARQUET_TARGET "$<IF:$<BOOL:${ARROW_BUILD_STATIC}>,Parquet::parquet_static,Parquet::parquet_shared>")

# ============================================================================
# Core static library: parsing, sort/merge, fd table, path resolution, replay
# engine.
# ============================================================================
add_library(TraceReplayCore STATIC
    src/core/Error.cpp
    src/core/SyscallClassify.cpp
    src/config/ReplayConfig.cpp
    src/io/ParquetEventReader.cpp
    src/io/EventMerger.cpp
    src/model/FdTable.cpp
    src/model/PathResolver.cpp
    src/replay/TimePacer.cpp
    src/replay/DryRunExecutor.cpp
    src/replay/SyscallExecutor.cpp
    src/replay/ReplayEngine.cpp
)
target_link_libraries(TraceReplayCore PUBLIC
    TraceReplayWarnings
    ${_ARROW_TARGET}
    ${_PARQUET_TARGET}
    nlohmann_json::nlohmann_json
)

# ============================================================================
# Executable entry point
# ============================================================================
add_executable(trace_replay src/main.cpp)
target_link_libraries(trace_replay PRIVATE TraceReplayCore)

# ----------------------------------------------------------------------------
# Unit tests (optional)
# ----------------------------------------------------------------------------
if(TR_TRACE_REPLAY_BUILD_TESTS)
    enable_testing()
    # TODO: once GoogleTest is wired up, add add_subdirectory(tests) here.
    message(STATUS "TR_TRACE_REPLAY_BUILD_TESTS=ON, but the test framework is not yet wired up.")
endif()
