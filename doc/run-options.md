# subproc::RunOptions

[Back to README](../README.md)

Configuration struct for [`subproc::run()`](run.md). Uses C++20 designated initializers for a clean, declarative syntax.

## Definition

```cpp
struct RunOptions {
    PipeVar cin{PipeOption::inherit};
    PipeVar cout{PipeOption::inherit};
    PipeVar cerr{PipeOption::inherit};
    bool create_no_window{false};
    bool detached_process{false};
    bool new_process_group{false};
    std::string cwd{};
    double timeout{-1};
    bool raise_on_nonzero{false};
    EnvMap env{};
};
```

## Fields

### `cin` — Standard Input

| Type | Default | Description |
|------|---------|-------------|
| `PipeVar` | `PipeOption::inherit` | Controls what the child process reads from stdin. |

Accepted values:
- `PipeOption::inherit` — child inherits the parent's stdin (default).
- `PipeOption::pipe` — creates a pipe; use `Popen::cin` handle to write to it.
- `PipeOption::close` — closes stdin immediately (child reads EOF).
- `std::string` — writes the string contents to the child's stdin, then closes the pipe.
- `PipeHandle` — uses a specific Windows `HANDLE` (e.g., from `pipe_create()` or `pipe_file()`).
- `std::istream*` — reads from a C++ input stream and feeds it to the child.
- `FILE*` — reads from a C file pointer and feeds it to the child.

### `cout` — Standard Output

| Type | Default | Description |
|------|---------|-------------|
| `PipeVar` | `PipeOption::inherit` | Controls where the child's stdout goes. |

Accepted values:
- `PipeOption::inherit` — child writes to the parent's stdout (default).
- `PipeOption::pipe` — captures output into `CompletedProcess::cout`.
- `PipeOption::close` — discards stdout (like redirecting to `/dev/null`).
- `PipeHandle` — redirects to a specific handle.
- `std::ostream*` — streams output to a C++ output stream in real time.
- `FILE*` — streams output to a C file pointer in real time.

### `cerr` — Standard Error

| Type | Default | Description |
|------|---------|-------------|
| `PipeVar` | `PipeOption::inherit` | Controls where the child's stderr goes. |

Accepts the same values as `cout`, plus:
- `PipeOption::cout` — merges stderr into stdout (like shell `2>&1`).

### `create_no_window`

| Type | Default | Description |
|------|---------|-------------|
| `bool` | `false` | If `true`, the child process is created without a console window. Maps to Win32 `CREATE_NO_WINDOW`. |

Useful for background tools or services where a visible console is unwanted.

### `detached_process`

| Type | Default | Description |
|------|---------|-------------|
| `bool` | `false` | If `true`, the child is detached from the parent's console. Maps to Win32 `DETACHED_PROCESS`. |

The child process will not have access to the parent's console. Often combined with `create_no_window`.

### `new_process_group`

| Type | Default | Description |
|------|---------|-------------|
| `bool` | `false` | If `true`, the child is created in a new process group. Maps to Win32 `CREATE_NEW_PROCESS_GROUP`. |

Required for sending `CTRL_BREAK_EVENT` signals to the child via `Popen::terminate()` or `Popen::send_signal()`. Without this, `CTRL_BREAK` cannot target the child specifically.

### `cwd`

| Type | Default | Description |
|------|---------|-------------|
| `std::string` | `""` (empty) | Working directory for the child process. Empty string means inherit the parent's working directory. |

### `timeout`

| Type | Default | Description |
|------|---------|-------------|
| `double` | `-1` | Maximum time in seconds to wait for the process. `-1` means wait indefinitely. |

If the process exceeds this timeout, `run()` sends `SIGTERM`, waits for exit, and throws [`TimeoutExpired`](exceptions.md#timeoutexpired).

### `raise_on_nonzero`

| Type | Default | Description |
|------|---------|-------------|
| `bool` | `false` | If `true`, `run()` throws [`CalledProcessError`](exceptions.md#calledprocesserror) when the exit code is non-zero. |

### `env`

| Type | Default | Description |
|------|---------|-------------|
| `EnvMap` (`std::map<std::string, std::string>`) | `{}` (empty) | Environment variables for the child process. Empty map means inherit the parent's environment. |

When non-empty, this **replaces** the child's entire environment. Use [`current_env_copy()`](environment.md#current_env_copy) to start from the current environment and modify it.

## Usage with Designated Initializers

```cpp
auto cp = subproc::run({"my_tool", "--verbose"},
    {.cout = PipeOption::pipe,
     .cerr = PipeOption::pipe,
     .new_process_group = true,
     .cwd = "C:/projects/my_tool",
     .timeout = 30.0,
     .raise_on_nonzero = true});
```

## Usage with RunBuilder

The same configuration via [`RunBuilder`](run-builder.md):

```cpp
auto cp = subproc::RunBuilder({"my_tool", "--verbose"})
    .cout(PipeOption::pipe)
    .cerr(PipeOption::pipe)
    .new_process_group(true)
    .cwd("C:/projects/my_tool")
    .timeout(30.0)
    .raise_on_nonzero(true)
    .run();
```

## See Also

- [`run()`](run.md) — uses `RunOptions` as its configuration
- [`RunBuilder`](run-builder.md) — fluent builder that wraps `RunOptions`
- [`PipeOption`](pipe-option.md) — enum values for pipe redirection
- [`CompletedProcess`](completed-process.md) — the result of `run()`
