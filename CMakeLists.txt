cmake_minimum_required(VERSION 3.20)

# ============================================================================
# vcpkg 集成（必须在 project() 之前）
#
# 与参考项目（Cubium）一致的写法：若环境提供 VCPKG_ROOT 则自动接入 vcpkg
# 工具链，从而在 manifest 模式下自动安装 arrow / nlohmann-json。
# 注意：Windows 上需先经 VsDevCmd.bat 注入 VS 开发环境，使 cl.exe 进入 PATH，
# 否则 vcpkg 无法定位 VS 实例（本机的 VS 18 预览版不被 vswhere 识别）。
# ============================================================================
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
elseif(EXISTS "E:/vcpkg/scripts/buildsystems/vcpkg.cmake" AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "E:/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

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
find_package(Arrow CONFIG REQUIRED)
find_package(Parquet CONFIG REQUIRED)

# JSON 配置解析，使用 nlohmann/json（header-only）。
find_package(nlohmann_json CONFIG REQUIRED)

# 选择 Arrow 链接目标：动态库优先（vcpkg x64-windows 默认动态）。
set(_ARROW_TARGET "$<IF:$<BOOL:${ARROW_BUILD_STATIC}>,Arrow::arrow_static,Arrow::arrow_shared>")
set(_PARQUET_TARGET "$<IF:$<BOOL:${ARROW_BUILD_STATIC}>,Parquet::parquet_static,Parquet::parquet_shared>")

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
    ${_ARROW_TARGET}
    ${_PARQUET_TARGET}
    nlohmann_json::nlohmann_json
)

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
