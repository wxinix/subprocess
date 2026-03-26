# subproc::run()

[Back to README](../README.md)

The primary entry point for running subprocesses. Launches a command, waits for it to finish, and returns a [`CompletedProcess`](completed-process.md) with the results. Similar in spirit to the C standard library's `system()` function, but with argument vector separation (no shell involved), pipe control, timeout support, and structured return values.

## Signatures

```cpp
// (1) Run a command with options
CompletedProcess run(CommandLine command, const RunOptions& options = {});

// (2) Run an already-opened Popen
CompletedProcess run(Popen& popen, bool check = false);
```

## Overload (1) — Run a command

Creates a subprocess from the given command line, waits for completion (subject to `timeout`), collects any piped output, and returns a `CompletedProcess`.

### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `command` | `CommandLine` (`std::vector<std::string>`) | The command and its arguments. The first element is the program name, resolved via `PATH`. |
| `options` | `const RunOptions&` | Configuration for stdin/stdout/stderr redirection, timeout, working directory, environment, and process creation flags. See [`RunOptions`](run-options.md). Defaults to inheriting all handles. |

### Behavior

1. Constructs a `Popen` from `command` and `options`.
2. Spawns background threads to collect stdout/stderr if piped.
3. Calls `try_wait(timeout)` to wait for the process.
4. If the process does not exit within the timeout, sends `SIGTERM`, waits for exit, then throws `TimeoutExpired`.
5. If `options.raise_on_nonzero` is `true` and the exit code is non-zero, throws `CalledProcessError`.
6. Returns a `CompletedProcess` with the exit code and captured output.

### Example

```cpp
// Simple command, no capture
subproc::run({"echo", "hello"});

// Capture stdout
auto cp = subproc::run({"echo", "hello"}, {.cout = PipeOption::pipe});
// cp.cout == "hello\r\n"

// With timeout and error checking
auto cp = subproc::run({"my_tool", "--process"},
    {.cout = PipeOption::pipe, .timeout = 10.0, .raise_on_nonzero = true});
```

## Overload (2) — Run from an existing Popen

Collects output from an already-opened `Popen`, waits for it to exit, and returns a `CompletedProcess`. This is useful when you have set up pipes between multiple processes and want to collect the final result.

### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `popen` | `Popen&` | A reference to an open subprocess. Its stdout/stderr pipes are drained and closed. |
| `check` | `bool` | If `true`, throws `CalledProcessError` unconditionally (used internally for error propagation). Default: `false`. |

### Example

```cpp
auto popen = subproc::RunBuilder({"sort"})
    .cin("cherry\napple\nbanana\n")
    .cout(PipeOption::pipe)
    .popen();

auto result = subproc::run(popen);
// result.cout == "apple\nbanana\ncherry\n"
```

## Exceptions

| Exception | When |
|-----------|------|
| [`CommandNotFoundError`](exceptions.md#commandnotfounderror) | The program in `command[0]` cannot be found on `PATH`. |
| [`SpawnError`](exceptions.md#spawnerror) | `CreateProcess` fails (e.g., access denied). |
| [`TimeoutExpired`](exceptions.md#timeoutexpired) | The process does not exit within `options.timeout` seconds. |
| [`CalledProcessError`](exceptions.md#calledprocesserror) | `raise_on_nonzero` is `true` and the exit code is non-zero. |

## See Also

- [`RunOptions`](run-options.md) — configuration struct
- [`RunBuilder`](run-builder.md) — fluent builder alternative
- [`CompletedProcess`](completed-process.md) — return type
- [`Popen`](popen.md) — for async process control
