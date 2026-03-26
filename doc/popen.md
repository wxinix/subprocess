# subproc::Popen

[Back to README](../README.md)

Represents a running subprocess. Unlike [`run()`](run.md) which blocks until completion, `Popen` gives you asynchronous control — you can poll, wait with timeout, send signals, and read/write pipes manually.

## Definition

```cpp
struct Popen {
    // Public fields
    PipeHandle cin{kBadPipeValue};
    PipeHandle cout{kBadPipeValue};
    PipeHandle cerr{kBadPipeValue};
    pid_t pid{0};
    int64_t returncode{kBadReturnCode};
    CommandLine args{};

    // Constructors
    Popen();
    Popen(const CommandLine& command, RunOptions options);

    // Move-only (no copy)
    Popen(Popen&&) noexcept;
    Popen& operator=(Popen&&) noexcept;

    // Lifecycle
    bool poll();
    int64_t wait(double timeout = -1.0);
    std::optional<int64_t> try_wait(double timeout);
    void close();
    void close_cin();

    // Signals
    bool send_signal(SigNum signum) const;
    bool terminate() const;
    bool kill() const;
    void enable_soft_kill(bool value);

    // Output management
    void ignore_cout();
    void ignore_cerr();
    void ignore_output();
};
```

## Construction

### Via RunBuilder (recommended)

```cpp
auto proc = subproc::RunBuilder({"my_server", "--port", "8080"})
    .cout(PipeOption::pipe)
    .new_process_group(true)
    .popen();
```

### Direct construction

```cpp
subproc::Popen proc({"echo", "hello"}, {.cout = PipeOption::pipe});
```

The process is launched immediately upon construction.

## Fields

| Field | Type | Description |
|-------|------|-------------|
| `cin` | `PipeHandle` | Write handle for the child's stdin. `kBadPipeValue` if not piped. |
| `cout` | `PipeHandle` | Read handle for the child's stdout. `kBadPipeValue` if not piped. |
| `cerr` | `PipeHandle` | Read handle for the child's stderr. `kBadPipeValue` if not piped. |
| `pid` | `pid_t` (`DWORD`) | The Windows process ID. `0` if the process has been closed. |
| `returncode` | `int64_t` | Exit code after the process has terminated. `kBadReturnCode` (-1000) while still running. |
| `args` | `CommandLine` | The command line used to launch the process. |

## Methods

### `poll()`

```cpp
bool poll();
```

Checks whether the process has exited **without blocking**.

- Returns `true` if the process has exited (and sets `returncode`).
- Returns `false` if the process is still running.

```cpp
auto proc = subproc::RunBuilder({"long_task"}).cout(PipeOption::pipe).popen();
while (!proc.poll()) {
    // do other work
    subproc::sleep_seconds(0.1);
}
std::println("Exit code: {}", proc.returncode);
```

### `wait(timeout)`

```cpp
int64_t wait(double timeout = -1.0);
```

Blocks until the process exits or the timeout expires.

- `timeout = -1.0` (default): waits indefinitely.
- Returns the exit code on success.
- Throws [`TimeoutExpired`](exceptions.md#timeoutexpired) if the timeout expires before the process exits.

```cpp
auto proc = subproc::RunBuilder({"slow_tool"}).popen();
try {
    int64_t rc = proc.wait(10.0);  // wait up to 10 seconds
    std::println("Exited with code {}", rc);
} catch (const subproc::TimeoutExpired&) {
    proc.terminate();
    proc.wait();
}
```

### `try_wait(timeout)`

```cpp
std::optional<int64_t> try_wait(double timeout);
```

Non-throwing version of `wait()`. Returns the exit code wrapped in `std::optional`, or `std::nullopt` if the timeout expires.

```cpp
if (auto rc = proc.try_wait(5.0)) {
    std::println("Exited with code {}", *rc);
} else {
    std::println("Still running after 5 seconds");
    proc.terminate();
    proc.wait();
}
```

### `close()`

```cpp
void close();
```

Cleans up the process:
1. Closes all pipe handles (`cin`, `cout`, `cerr`).
2. Waits for the process to exit (if still running).
3. Closes the Windows process and thread handles.
4. Resets all fields to their default values.

Always call `close()` when you are done with a `Popen` to avoid handle leaks. The destructor also calls `close()`.

### `close_cin()`

```cpp
void close_cin();
```

Closes only the stdin pipe. This sends EOF to the child process, which is often needed to signal that input is complete.

```cpp
auto proc = subproc::RunBuilder({"cat"})
    .cin(PipeOption::pipe)
    .cout(PipeOption::pipe)
    .popen();

subproc::pipe_write_fully(proc.cin, "hello", 5);
proc.close_cin();  // signal EOF — cat will now exit

auto output = subproc::pipe_read_all(proc.cout);
proc.close();
```

### `send_signal(signum)`

```cpp
bool send_signal(SigNum signum) const;
```

Sends a signal to the process. Returns `true` on success, `false` if the process has already exited.

| Signal | Windows action |
|--------|----------------|
| `SigNum::PSIGKILL` | Calls `TerminateProcess` (hard kill). Also terminates child processes. |
| `SigNum::PSIGINT` | Sends `CTRL_C_EVENT` to the entire console process group. |
| Any other signal | Sends `CTRL_BREAK_EVENT` to the process group identified by `pid`. |

**Important**: `CTRL_BREAK_EVENT` requires `new_process_group = true` to target only the child.

### `terminate()`

```cpp
bool terminate() const;
```

Sends `CTRL_BREAK_EVENT` (equivalent to `send_signal(SigNum::PSIGTERM)`). This is a graceful shutdown request — the child process can catch and handle it.

### `kill()`

```cpp
bool kill() const;
```

By default, calls `TerminateProcess` — an immediate, forceful termination that cannot be caught by the child.

If `enable_soft_kill(true)` has been called, `kill()` behaves like `terminate()` instead, sending `CTRL_BREAK_EVENT`.

### `enable_soft_kill(value)`

```cpp
void enable_soft_kill(bool value);
```

When enabled, `kill()` sends `CTRL_BREAK_EVENT` instead of calling `TerminateProcess`. This allows the child to perform cleanup before exiting.

```cpp
auto proc = subproc::RunBuilder({"server"}).new_process_group(true).popen();
proc.enable_soft_kill(true);
proc.kill();  // sends CTRL_BREAK instead of TerminateProcess
proc.wait();
proc.close();
```

### `ignore_cout()` / `ignore_cerr()` / `ignore_output()`

```cpp
void ignore_cout();
void ignore_cerr();
void ignore_output();
```

Spawns a background thread that drains and discards the pipe, then closes the handle. Use these when you created a pipe but don't need the output (prevents the child from blocking on a full pipe buffer).

- `ignore_cout()` — drains and closes `cout`.
- `ignore_cerr()` — drains and closes `cerr`.
- `ignore_output()` — drains and closes both `cout` and `cerr`.

## Ownership and Lifetime

- `Popen` is **move-only** (no copy).
- The destructor calls `close()`, which waits for the process to exit. If you need the process to outlive the `Popen` object, use `detached_process = true`.
- Pipe handles are owned by the `Popen` instance. Closing the `Popen` closes the pipes.

## See Also

- [`run()`](run.md) — simpler synchronous API
- [`RunBuilder`](run-builder.md) — fluent builder, has `.popen()` method
- [Pipe Functions](pipe-functions.md) — for reading/writing to `Popen` pipe handles
- [Signal Handling](signals.md) — details on Windows signal behavior
