cmake_minimum_required(VERSION 3.20)

# ============================================================================
# trace-replay —— 按时间序列回放 rawproc 排序后 Parquet trace 的事件流
#
# 设计核心（与 rawproc 分工对齐）：
#   * 输入是 rawproc 产出的 events_tsorted/bucket=xxxxxx，桶间全局有序、
#     桶内按 (machine_ts, log_offset) 有序，故顺序归并即全局时间序。
#   * replay 以 fd 为准而非 path：IO 类操作只信赖 (pid, fd)，经 per-pid fd
#     表映射到本进程真实打开的 fd，对真实 fd 执行 syscall（openat-replay）。
#   * 在受限沙箱根目录下真实执行 syscall，路径防穿越。
# ============================================================================

project(trace_replay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认 Release，trace 数据量大、需要性能
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(TR_TRACE_REPLAY_BUILD_TESTS "构建 trace-replay 的单元测试" OFF)

# 编译警告配置（公共）
add_library(TraceReplayWarnings INTERFACE)
target_include_directories(TraceReplayWarnings INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
include(cmake/CompilerWarnings.cmake)
set_project_warnings(TraceReplayWarnings)

# ----------------------------------------------------------------------------
# 第三方依赖
# ----------------------------------------------------------------------------

# 解析 rawproc 产出的 Parquet，依赖 Apache Arrow。
find_package(Arrow QUIET)
find_package(Parquet QUIET)
if(NOT Arrow_FOUND OR NOT Parquet_FOUND)
    message(FATAL_ERROR
        "未找到 Apache Arrow/Parquet。请安装并在 CMake 中配置 Arrow_DIR/Parquet_DIR，\n"
        "例如 vcpkg: ./vcpkg install arrow[parquet] \n"
        "或 conan: conan install arrow/... -o arrow:parquet=True")
endif()

# JSON 配置解析，使用 nlohmann/json（header-only，常见包管理器均可获取）。
find_package(nlohmann_json QUIET CONFIG)
if(NOT nlohmann_json_FOUND)
    message(WARNING
        "未找到 nlohmann_json，将假定其头文件位于标准搜索路径。\n"
        "建议: vcpkg install nlohmann-json 或 conan install nlohmann_json/...")
endif()

# ============================================================================
# 核心静态库：解析、排序归并、fd 表、路径解析、replay 引擎
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
    parquet::parquet_shared
    arrow::arrow_shared
)
if(nlohmann_json_FOUND)
    target_link_libraries(TraceReplayCore PUBLIC nlohmann_json::nlohmann_json)
else()
    target_compile_definitions(TraceReplayCore PUBLIC TRACE_REPLAY_HAVE_NLOHMANN_JSON)
endif()

# ============================================================================
# 可执行入口
# ============================================================================
add_executable(trace_replay src/main.cpp)
target_link_libraries(trace_replay PRIVATE TraceReplayCore)

# ----------------------------------------------------------------------------
# 单元测试（可选）
# ----------------------------------------------------------------------------
if(TR_TRACE_REPLAY_BUILD_TESTS)
    enable_testing()
    # TODO: 接入 GoogleTest 后在此添加 add_subdirectory(tests)
    message(STATUS "TR_TRACE_REPLAY_BUILD_TESTS=ON，但测试框架尚未接入。")
endif()
