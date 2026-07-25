# trace-replay

按时间序列回放 rawproc 排序后 Parquet trace 的事件流，在受限沙箱内真实执行 syscall。

## 与 rawproc 的分工

本项目对应 context.md 中提到的 **openat-replay**：rawproc（`rawproc_spark.py`）只做解析、挂载映射、按 `(machine_ts, log_offset)` 分桶排序并产出 `events_tsorted/bucket=NNNNNN`；它**不碰 fd**。本项目消费那份已排序的输出，做真正的回放。

核心原则：**以 fd 为准，而不是 path。**

- rawproc 排序保证：桶间全局有序、无重叠，桶内按 `(machine_ts, log_offset)` 有序。
- 因此 `EventMerger` 只需顺序归并各桶，即得到全局时间序事件流——这就是 replay 的时间序列来源。
- IO 类操作（`read`/`write`/`pread64`/`pwrite64`/`readv`/`writev`）的 `path` 列不可信甚至为空，真正携带文件信息的是 `(pid, arg1=fd)`。回放时只信赖 `(pid, fd)`，经 per-pid fd 表映射到本回放进程真实打开的 fd，再对其执行真实 syscall。

## 目录结构

```
trace-replay/
├── CMakeLists.txt
├── cmake/CompilerWarnings.cmake
├── config.example.json          # JSON 配置示例
├── include/trace_replay/
│   ├── core/    Types/Assert/Error/Result/TraceEvent/SyscallClassify
│   ├── config/  ReplayConfig
│   ├── io/      IEventReader/ParquetEventReader/EventMerger
│   ├── model/   FdTable/PathResolver
│   └── replay/  IExecutor/TimePacer/DryRunExecutor/SyscallExecutor/ReplayEngine
├── src/         （与 include 一一对应的 .cpp + main.cpp）
└── README.md
```

## 数据流

```
events_tsorted/bucket=NNNNNN  ─┐
events_tsorted/bucket=NNNNNN  ─┤  ParquetEventReader（每桶一个，桶内已排序）
events_tsorted/bucket=NNNNNN  ─┘
                  │
            EventMerger  ── K 路小顶堆归并（machine_ts, log_offset）── 全局时间序
                  │
            ReplayEngine ── 过滤(pid/side) ── TimePacer(节拍) ── IExecutor
                                                                        │
                                          ┌─────────────────────────────┴──┐
                                  DryRunExecutor                      SyscallExecutor
                                  （推演 fd 表/路径，打印）          （沙箱内真实 syscall）
```

## fd 表语义

| 原始 trace 事件 | fd 表动作 | 真实 syscall |
|---|---|---|
| `openat` 成功（ret=fd） | 登记 `(pid, origFd=ret) → (ourFd, path)` | 本进程 `::open` 得 ourFd |
| `read`/`write`（arg1=fd） | 查表得 ourFd | 对 ourFd `::read`/`::write` |
| `close`（arg1=fd） | 注销，取回 ourFd | `::close(ourFd)` |
| `stat`/`mkdir`/`unlink`/`rename` | 解析路径（防穿越） | 对应真实 syscall |

> 说明：trace 不含数据负载，故 `read` 用零缓冲、`write` 写零字节占位，目的是还原"该 fd 上发生 N 字节 IO"的**时间序列语义**，而非还原数据内容。

## 配置（JSON）

见 `config.example.json`。关键字段：

| 字段 | 含义 |
|---|---|
| `events_root` | rawproc 产出根目录（其下应有 `events_tsorted/`） |
| `sandbox_root` | 真实 syscall 的根目录，所有路径强制落在其下（防穿越） |
| `bucket_width` | 桶目录名宽度（默认 6，对齐 rawproc） |
| `bucket_min`/`bucket_max` | 只回放该范围内的桶（闭区间，`-1` 表示无上限） |
| `pace_mode` | `fast` / `real` / `scaled` |
| `speed` | `scaled` 模式倍速 |
| `side_filter` | `all` / `source` / `target` |
| `skip_unparsed` | 是否跳过 `_unparsed` / `null_ts` 桶 |
| `pid_filter` | 只回放这些 pid（数组，空=全部） |
| `dry_run` | `true`=仅推演不执行 syscall；`false`=真实执行 |
| `max_io_bytes` | 单次 IO 上限（默认 1 MiB） |
| `continue_on_error` | syscall 失败时是否继续 |

## 用法

```
trace_replay config.json
```

## 构建依赖

- C++20
- Apache Arrow（含 Parquet）：`vcpkg install arrow[parquet]` 或 conan
- nlohmann_json：`vcpkg install nlohmann-json`
- 真实 syscall 执行器（`SyscallExecutor`）依赖 POSIX（`openat`/`read`/`rename` 等），在 Linux 下构建运行；Windows 下仅 `DryRunExecutor` 路径与解析层可编译。

## 已知 TODO

- `dup`/`dup2`/`dup3`、`exit(group)` 对 fd 表的影响尚未覆盖。
- `getdents`/`utimensat`/xattr 等暂未真实执行（标记 skipped）。
- 单元测试框架尚未接入（`TR_TRACE_REPLAY_BUILD_TESTS` 占位）。
