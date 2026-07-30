# trace-replay

Replays the event stream of a rawproc-sorted Parquet trace in time-series order, executing real syscalls inside a restricted sandbox.

## Division of labor with rawproc

This project corresponds to the **openat-replay** mentioned in context.md: rawproc (`rawproc_spark.py`) only parses, performs mount mapping, buckets by `(machine_ts, log_offset)`, sorts, and produces `events_tsorted/bucket=NNNNNN`; it **never touches fd**. This project consumes that sorted output and performs the actual replay.

Core principle: **fd takes precedence over path.**

- rawproc sort guarantees: globally ordered across buckets, no overlap, and within each bucket ordered by `(machine_ts, log_offset)`.
- Therefore `EventMerger` only needs to sequentially merge each bucket to obtain a globally time-ordered event stream — this is the source of the replay time series.
- The `path` column of IO operations (`read`/`write`/`pread64`/`pwrite64`/`readv`/`writev`) is unreliable or even empty; the real file information is carried by `(pid, arg1=fd)`. During replay only `(pid, fd)` is trusted, mapped through a per-pid fd table to the fd actually opened by this replay process, on which the real syscall is then executed.

## Directory structure

```
trace-replay/
├── CMakeLists.txt
├── cmake/CompilerWarnings.cmake
├── config.example.json          # JSON config example
├── include/trace_replay/
│   ├── core/    Types/Assert/Error/Result/TraceEvent/SyscallClassify
│   ├── config/  ReplayConfig
│   ├── io/      IEventReader/ParquetEventReader/EventMerger
│   ├── model/   FdTable/PathResolver
│   └── replay/  IExecutor/TimePacer/DryRunExecutor/SyscallExecutor/ReplayEngine
├── src/         (.cpp files one-to-one with include + main.cpp)
└── README.md
```

## Data flow

```
events_tsorted/bucket=NNNNNN  ─┐
events_tsorted/bucket=NNNNNN  ─┤  ParquetEventReader (one per bucket, sorted within)
events_tsorted/bucket=NNNNNN  ─┘
                  │
            EventMerger  ── K-way min-heap merge (machine_ts, log_offset) ── global time order
                  │
            ReplayEngine ── filter(pid/side) ── TimePacer(pacing) ── IExecutor
                                                                        │
                                          ┌─────────────────────────────┴──┐
                                  DryRunExecutor                      SyscallExecutor
                                  (simulate fd table/path, print)     (real syscalls in sandbox)
```

## fd table semantics

| Original trace event | fd table action | Real syscall |
|---|---|---|
| `openat` success (ret=fd) | register `(pid, origFd=ret) → (ourFd, path)` | this process `::open` to get ourFd |
| `read`/`write` (arg1=fd) | look up ourFd in table | `::read`/`::write` on ourFd |
| `close` (arg1=fd) | unregister, reclaim ourFd | `::close(ourFd)` |
| `stat`/`mkdir`/`unlink`/`rename` | resolve path (with traversal protection) | corresponding real syscall |

> Note: the trace carries no data payload, so `read` uses a zero buffer and `write` writes zero bytes as a placeholder. The goal is to reproduce the **time-series semantics** of "N bytes of IO occurred on this fd", not the data content.

## Configuration (JSON)

See `config.example.json`. Key fields:

| Field | Meaning |
|---|---|
| `events_root` | rawproc output root directory (should contain `events_tsorted/` underneath) |
| `sandbox_root` | root directory for real syscalls; all paths are forced to stay under it (traversal protection) |
| `bucket_width` | bucket directory name width (default 6, aligned with rawproc) |
| `bucket_min`/`bucket_max` | only replay buckets within this range (closed interval; `-1` means no upper bound) |
| `pace_mode` | `fast` / `real` / `scaled` |
| `speed` | speed multiplier for `scaled` mode |
| `side_filter` | `all` / `source` / `target` |
| `skip_unparsed` | whether to skip `_unparsed` / `null_ts` buckets |
| `pid_filter` | only replay these pids (array; empty = all) |
| `dry_run` | `true` = simulate only, do not execute syscalls; `false` = real execution |
| `max_io_bytes` | per-IO upper bound (default 1 MiB) |
| `continue_on_error` | whether to continue on syscall failure |

## Usage

```
trace_replay config.json
```

## Build dependencies

- C++20
- Apache Arrow (including Parquet): `vcpkg install arrow[parquet]`, or auto-installed via manifest
- nlohmann_json: `vcpkg install nlohmann-json`

## Build (Windows / clang)

The project provides `scripts/configure.bat`, which automatically injects the VS development environment (vcpkg needs `cl.exe` to locate the toolchain; the VS Preview on this machine is not recognized by `vswhere`, so the environment must be injected first).

```bat
:: Configure (manifest mode auto-installs arrow/nlohmann-json)
scripts\configure.bat

:: Configure + build
scripts\configure.bat build
```

The script uses `clang++` (GNU-style command line) + Ninja Multi-Config. The output is in
`build/bin/Debug/trace_replay.exe` and `build/bin/Release/trace_replay.exe`; the Arrow/Parquet
DLLs required at runtime are automatically deployed by vcpkg to the same directory.

> The real syscall executor (`SyscallExecutor`) depends on POSIX (`openat`/`read`/`rename`, etc.)
> and only builds/runs on Linux. On Windows, `SyscallExecutor` is a placeholder implementation
> (its `execute` always returns "platform not supported"); only the `DryRunExecutor` path and the
> resolution layer can actually run. For real replay on Windows, the syscall layer must be ported
> to the Win32 API.

## Known TODOs

- The effects of `dup`/`dup2`/`dup3` and `exit(group)` on the fd table are not yet covered.
- `getdents`/`utimensat`/xattr etc. are not yet executed for real (marked as skipped).
- The unit test framework is not yet wired up (`TR_TRACE_REPLAY_BUILD_TESTS` placeholder).
- On Windows, `SyscallExecutor` is a placeholder implementation; real replay requires porting to the Win32 API.
