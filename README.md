# subprocess

A modern C++23 subprocess library inspired by Python's `subprocess` module, with first-class Windows support.

> **Note:** This library is a **ground-up rewrite** inspired by the original [benman64/subprocess](https://github.com/benman64/subprocess). While the original project (C++17, cross-platform) provided the foundational design and API philosophy, this codebase has been **completely rewritten** to embrace modern C++23 — including deducing `this`, concepts, `std::expected`-style patterns, `std::format`, `std::print`, designated initializers, and `std::ranges`. The architecture, implementation details, error handling, and Windows backend are all new. If you need C++17 compatibility or full POSIX support, use the [original repository](https://github.com/benman64/subprocess).

Maintained by [@wxinix](https://github.com/wxinix).

## Features

- **Python-like API** with C++20 designated initializers and C++23 deducing this
- **Fluent builder pattern** via `RunBuilder` with perfect-forwarding method chains
- **Pipe management** connecting stdout of process A to stdin of process B
- **Environment utilities** with `subprocess::cenv["VAR"] = "value"` syntax
- **RAII guards** for environment (`EnvGuard`) and working directory (`CwdGuard`)
- **Cross-platform `find_program`** with caching and Python 3 detection on Windows
- **Stream redirection** to C++ streams (`std::istream*`, `std::ostream*`), `FILE*`, or `std::string`
- **Signal handling** with `send_signal()`, `terminate()`, `kill()`, and soft-kill mode
- **Timeout support** via `try_wait()` (returns `std::optional`) and `wait()` (throws `TimeoutExpired`)

## Requirements

- **C++23** (MSVC 19.37+, GCC 14+, Clang 17+)
- **CMake 3.27+**
- On Unix-like systems, link with pthread

## Building

```bash
# Debug build
cmake --preset debug
cmake --build build/debug

# Release build
cmake --preset release
cmake --build build/release

# Run tests
build/debug/test/Debug/basic_test.exe --verbose   # Windows/MSVC
build/debug/test/basic_test --verbose              # Unix/Ninja
```

## Quick Start

Include the single header and you're ready:

```cpp
#include <subprocess.h>
```

### Running a Command

The simplest way to run a command. Output goes to the parent's terminal:

```cpp
subprocess::run({"echo", "hello", "world"});
```

The first element of the command vector is the executable; the rest are arguments. On Windows, the library automatically searches PATH for the executable (e.g., a custom `echo.exe`).

### Capturing Output

To capture a child process's stdout into a `std::string`, set `.cout = PipeOption::pipe`. The returned `CompletedProcess` struct contains the captured data:

```cpp
// Using C++20 designated initializers (concise)
auto cp = subprocess::run({"echo", "hello"}, {.cout = PipeOption::pipe});
// cp.cout == "hello\n", cp.returncode == 0

// Using the fluent RunBuilder API (more options, same result)
auto cp2 = subprocess::RunBuilder({"echo", "hello"})
    .cout(PipeOption::pipe)
    .run();
```

Both styles produce identical results. Designated initializers are concise for simple cases; `RunBuilder` chains are useful when you need to set many options or build configurations programmatically.

### Capturing Both stdout and stderr

```cpp
auto cp = subprocess::run({"my_program"},
    {.cout = PipeOption::pipe, .cerr = PipeOption::pipe});

// cp.cout contains stdout, cp.cerr contains stderr
```

To merge stderr into stdout (like shell `2>&1`), redirect cerr to cout:

```cpp
auto cp = subprocess::run({"my_program"},
    {.cout = PipeOption::pipe, .cerr = PipeOption::cout});
// cp.cout now contains both stdout and stderr interleaved
```

### Sending Data to stdin

Pass a `std::string` to the `cin` option. The library spawns a background thread to pipe the data, so there's no deadlock risk:

```cpp
auto cp = subprocess::run({"cat"},
    subprocess::RunBuilder().cin("piped data").cout(PipeOption::pipe));
// cp.cout == "piped data"
```

You can also connect a `std::istream*`, `FILE*`, or a raw pipe handle as the input source.

### Checking for Errors

By default, `run()` doesn't throw on non-zero exit codes. Enable `raise_on_nonzero` to get Python's `check=True` behavior:

```cpp
try {
    auto cp = subprocess::run({"false"}, {.raise_on_nonzero = true});
} catch (const subprocess::CalledProcessError& e) {
    // e.returncode, e.cmd, e.cout, e.cerr are all available
    std::println("Command failed with code {}", e.returncode);
}
```

Without `raise_on_nonzero`, simply inspect `cp.returncode` or use the bool conversion:

```cpp
auto cp = subprocess::run({"my_program"}, {.cout = PipeOption::pipe});
if (cp) {
    // returncode == 0, success
} else {
    // non-zero exit
}
```

### Timeouts

Set a timeout in seconds. If the process doesn't finish in time, the library sends `SIGTERM`, waits for exit, then throws `TimeoutExpired`:

```cpp
try {
    subprocess::run({"sleep", "30"}, {.timeout = 5.0});
} catch (const subprocess::TimeoutExpired& e) {
    // e.timeout == 5.0, e.cout and e.cerr contain any captured data
}
```

### Custom Working Directory and Environment

```cpp
// Run in a different directory
auto cp = subprocess::run({"ls"}, {.cout = PipeOption::pipe, .cwd = "/tmp"});

// Run with custom environment (inherits nothing unless you copy first)
auto env = subprocess::current_env_copy();  // start from current env
env["MY_VAR"] = "my_value";
env.erase("UNWANTED_VAR");
auto cp2 = subprocess::run({"printenv", "MY_VAR"},
    {.cout = PipeOption::pipe, .env = env});
// cp2.cout == "my_value\n"
```

If `.env` is left empty (default), the child inherits the parent's full environment.

### Asynchronous Control with Popen

For long-running processes or interactive communication, use `Popen` directly:

```cpp
auto popen = subprocess::RunBuilder({"long_running_cmd"})
    .cout(PipeOption::pipe)
    .new_process_group(true)
    .popen();

// Non-blocking poll: returns true when process has exited
while (!popen.poll()) {
    // ... do other work ...
}
std::println("exit code: {}", popen.returncode);

// Or wait with timeout using std::optional (no exceptions)
if (auto rc = popen.try_wait(5.0)) {
    std::println("exited with {}", *rc);
} else {
    // Timed out - terminate gracefully
    popen.terminate();
    popen.wait();  // wait for actual exit after signal
}

popen.close();  // closes pipes, waits if not already waited
```

`Popen` is move-only (no copy). Its destructor calls `close()`, which waits for the process and cleans up handles.

### Piping Between Processes

Connect the output of one process to the input of another, like a shell pipe `echo hello | cat`:

```cpp
// Create a pipe pair (not inheritable by default)
auto pp = subprocess::pipe_create(false);

// echo writes to the pipe's output end
auto writer = subprocess::RunBuilder({"echo", "hello"})
    .cout(pp.output)
    .popen();

// cat reads from the pipe's input end, captures to our pipe
auto reader = subprocess::RunBuilder({"cat"})
    .cin(pp.input)
    .cout(PipeOption::pipe)
    .popen();

// Close our copies of the pipe ends (child processes have their own)
pp.close();

// Collect cat's output
auto result = subprocess::run(reader);
// result.cout == "hello\n"

writer.close();
reader.close();
```

### Low-Level Pipe I/O

For manual byte-level communication with a running process:

```cpp
auto popen = subprocess::RunBuilder({"cat"})
    .cin(PipeOption::pipe)
    .cout(PipeOption::pipe)
    .popen();

// Write to the process's stdin
const char* msg = "hello\n";
subprocess::pipe_write(popen.cin, msg, strlen(msg));
popen.close_cin();  // signal EOF to the child

// Read from the process's stdout
auto output = subprocess::pipe_read_all(popen.cout);
popen.close();
```

### Environment Management

The global `subprocess::cenv` object provides Python-like `os.environ` access:

```cpp
// Read
std::string path = subprocess::cenv["PATH"];

// Write (supports string, int, bool, float)
subprocess::cenv["MY_VAR"] = "value";
subprocess::cenv["COUNT"] = 42;
subprocess::cenv["DEBUG"] = true;   // stored as "1"

// Delete
subprocess::cenv["MY_VAR"] = nullptr;

// Check existence
if (subprocess::cenv["MY_VAR"]) { /* exists and non-empty */ }
```

Use `EnvGuard` for temporary environment changes that auto-restore on scope exit:

```cpp
{
    subprocess::EnvGuard guard;  // snapshots env + cwd
    subprocess::cenv["PATH"] = "/custom/path";
    subprocess::set_cwd("/tmp");
    // ... run subprocesses with modified env ...
}  // PATH and cwd restored here
```

For thread safety, copy the environment and pass it explicitly:

```cpp
auto env = subprocess::current_env_copy();  // returns std::map<string,string>
env["THREAD_SAFE"] = "yes";
subprocess::run({"cmd"}, {.env = env});
// Parent env is untouched; child gets the modified copy
```

### Windows-Specific Options

```cpp
auto cp = subprocess::run({"background_tool"},
    {.create_no_window = true,      // no console window
     .detached_process = true,      // not attached to parent console
     .new_process_group = true});   // separate process group for signals
```

### Signal Handling

```cpp
auto popen = subprocess::RunBuilder({"server"})
    .new_process_group(true)
    .popen();

popen.send_signal(subprocess::SigNum::PSIGTERM);  // graceful shutdown
popen.terminate();  // same as SIGTERM (CTRL_BREAK on Windows)
popen.kill();       // SIGKILL (TerminateProcess on Windows)

// Soft-kill mode: make kill() send SIGTERM instead of SIGKILL
popen.enable_soft_kill(true);
popen.kill();  // now sends SIGTERM, not SIGKILL
```

### Advanced Pipe Operations

For fine-grained control over pipe I/O beyond `pipe_read` / `pipe_write` / `pipe_read_all`:

```cpp
auto pp = subprocess::pipe_create(false);

// Peek: check how many bytes are available without consuming them
ssize_t available = subprocess::pipe_peek_bytes(pp.input);

// Read some: blocks until at least 1 byte arrives, then drains whatever is buffered
char buf[4096];
ssize_t n = subprocess::pipe_read_some(pp.input, buf, sizeof(buf));

// Write fully: retries partial writes until the entire buffer is sent
const std::string data = "large payload...";
ssize_t written = subprocess::pipe_write_fully(pp.output, data.c_str(), data.size());
// Returns total bytes written, or negative on error

// Wait for data: block until readable, with timeout
int status = subprocess::pipe_wait_for_read(pp.input, 5.0);
// status: 1 = data available, 0 = timeout, -1 = error/closed

// Toggle blocking mode
subprocess::pipe_set_blocking(pp.input, false);  // non-blocking
subprocess::pipe_set_blocking(pp.input, true);   // blocking (default)
```

**File as pipe handle** — open a file and use it as a pipe handle for stdin/stdout redirection:

```cpp
// Read from a file as if it were a pipe
auto in_handle = subprocess::pipe_file("input.txt", "r");
auto cp = subprocess::run({"my_tool"}, RunBuilder().cin(in_handle).cout(PipeOption::pipe));
(void)subprocess::pipe_close(in_handle);

// Write process output directly to a file
auto out_handle = subprocess::pipe_file("output.txt", "w");
subprocess::run({"echo", "hello"}, RunBuilder().cout(out_handle));
(void)subprocess::pipe_close(out_handle);
```

## API Reference

### `subprocess::run()`

Runs a command to completion and returns a `CompletedProcess`.

```cpp
CompletedProcess run(CommandLine command, const RunOptions& options = {});
CompletedProcess run(Popen& popen, bool check = false);
```

### `RunOptions`

| Field               | Type          | Default   | Description                                                    |
|---------------------|---------------|-----------|----------------------------------------------------------------|
| `cin`               | `PipeVar`     | `inherit` | stdin: `PipeOption`, `std::string`, handle, stream, or `FILE*` |
| `cout`              | `PipeVar`     | `inherit` | stdout redirection                                             |
| `cerr`              | `PipeVar`     | `inherit` | stderr redirection                                             |
| `cwd`               | `std::string` | `""`      | Working directory for child process                            |
| `env`               | `EnvMap`      | `{}`      | Environment variables (empty = inherit)                        |
| `timeout`           | `double`      | `-1`      | Timeout in seconds (-1 = no timeout)                           |
| `raise_on_nonzero`  | `bool`        | `false`   | Throw `CalledProcessError` on non-zero exit                    |
| `new_process_group` | `bool`        | `false`   | Create a new process group                                     |
| `create_no_window`  | `bool`        | `false`   | Windows: no console window                                     |
| `detached_process`  | `bool`        | `false`   | Windows: detached from parent console                          |

### `PipeOption`

| Value      | Description             | Python Equivalent       |
|------------|-------------------------|-------------------------|
| `inherit`  | Inherit parent's handle | Default behavior        |
| `pipe`     | Create a new pipe       | `subprocess.PIPE`       |
| `cout`     | Redirect to stdout      | `subprocess.STDOUT`     |
| `cerr`     | Redirect to stderr      | *(no equivalent)*       |
| `close`    | Close the descriptor    | `subprocess.DEVNULL`    |
| `none`     | No file descriptor      | *(no equivalent)*       |
| `specific` | Use a provided handle   | *(via handle argument)* |

### `CompletedProcess`

| Field             | Type          | Description                             |
|-------------------|---------------|-----------------------------------------|
| `args`            | `CommandLine` | Command that was executed               |
| `returncode`      | `int64_t`     | Exit code (negative = killed by signal) |
| `cout`            | `std::string` | Captured stdout (empty if not piped)    |
| `cerr`            | `std::string` | Captured stderr (empty if not piped)    |
| `operator bool()` |               | `true` if `returncode == 0`             |

### `Popen`

| Method                | Description                                            |
|-----------------------|--------------------------------------------------------|
| `poll()`              | Returns `true` if process has exited                   |
| `wait(timeout)`       | Blocks until exit; throws `TimeoutExpired` on timeout  |
| `try_wait(timeout)`   | Returns `std::optional<int64_t>`; `nullopt` on timeout |
| `send_signal(sig)`    | Sends a signal to the process                          |
| `terminate()`         | Sends `SIGTERM` (`CTRL_BREAK_EVENT` on Windows)        |
| `kill()`              | Sends `SIGKILL` (`TerminateProcess` on Windows)        |
| `close()`             | Closes all pipes, waits for exit, cleans up handles    |
| `close_cin()`         | Closes stdin pipe (signals EOF to child)               |
| `enable_soft_kill(b)` | If true, `kill()` sends `SIGTERM` instead of `SIGKILL` |

### Pipe Functions

| Function | Description |
|----------|-------------|
| `pipe_create(inheritable)` | Creates a `PipePair` (input + output handles) |
| `pipe_close(handle)` | Closes a pipe handle |
| `pipe_read(handle, buf, size)` | Reads up to `size` bytes (blocks if empty) |
| `pipe_write(handle, buf, size)` | Writes up to `size` bytes |
| `pipe_read_all(handle)` | Reads until EOF, returns `std::string` |
| `pipe_write_fully(handle, buf, size)` | Writes entire buffer, retrying partial writes |
| `pipe_peek_bytes(handle)` | Returns bytes available without consuming them |
| `pipe_read_some(handle, buf, size)` | Blocks for 1 byte, then drains available data |
| `pipe_wait_for_read(handle, seconds)` | Waits for data: 1=ready, 0=timeout, -1=error |
| `pipe_set_blocking(handle, block)` | Toggles blocking/non-blocking mode |
| `pipe_set_inheritable(handle, inherit)` | Controls whether child processes inherit the handle |
| `pipe_file(filename, mode)` | Opens a file as a pipe handle (`"r"`, `"w"`, `"r+"`) |
| `pipe_ignore_and_close(handle)` | Spawns a thread to drain and close the pipe |

### Exception Hierarchy

```
std::runtime_error
  └── SubprocessError
        ├── OSError
        │     ├── SpawnError
        │     └── (general OS errors)
        ├── CommandNotFoundError
        ├── CalledProcessError  (returncode, cmd, cout, cerr)
        └── TimeoutExpired      (timeout, cmd, cout, cerr)
```

## Comparison with Python's `subprocess`

| Python                | C++                                              | Notes                              |
|-----------------------|--------------------------------------------------|------------------------------------|
| `subprocess.run()`    | `subprocess::run()`                              | Full equivalent                    |
| `subprocess.Popen()`  | `Popen` / `RunBuilder().popen()`                 | Full equivalent                    |
| `Popen.communicate()` | Built into `run()`                               | Automatic pipe collection          |
| `Popen.poll()`        | `Popen::poll()`                                  | Identical semantics                |
| `Popen.wait(timeout)` | `Popen::wait()` / `try_wait()`                   | `try_wait` returns `std::optional` |
| `check=True`          | `raise_on_nonzero = true`                        | Throws `CalledProcessError`        |
| `input="data"`        | `RunBuilder().cin("data")`                       | String piped to stdin              |
| `capture_output=True` | `.cout(PipeOption::pipe).cerr(PipeOption::pipe)` | Explicit per-stream                |
| `shell=True`          | *Not implemented*                                | By design; use explicit commands   |
| `text=True`           | *Not implemented*                                | C++ strings are byte strings       |
| `subprocess.PIPE`     | `PipeOption::pipe`                               | Equivalent                         |
| `subprocess.DEVNULL`  | `PipeOption::close`                              | Equivalent                         |
| `subprocess.STDOUT`   | `PipeOption::cout`                               | Redirect stderr to stdout          |
| `os.environ`          | `subprocess::cenv`                               | Similar get/set syntax             |

### Not Implemented (By Design)

- **`shell=True`**: Security risk; use explicit command vectors instead.
- **`text/encoding`**: C++ `std::string` is bytes; handle encoding at the application level.
- **`preexec_fn`**: Platform-specific; use `ProcessBuilder` for low-level control.
- **`check_output()`**: Use `run()` with `.cout(PipeOption::pipe).raise_on_nonzero(true)`.

## C++23 Features Used

- **Deducing this** for `RunBuilder` method chains (perfect forwarding on temporaries)
- **`std::visit` with `overloaded`** pattern for variant dispatch
- **`std::string::contains()`** and **`starts_with()`** for cleaner string checks
- **`std::optional`** for `try_wait()` — no-throw timeout handling
- **`std::print` / `std::println`** in test utilities
- **`std::ranges::replace`**, **`std::ranges::any_of`**, and **`std::ranges::to`** for algorithm clarity
- **`using enum`** for cleaner switch statements
- **Concepts and `requires` clauses** for template constraints
- **Designated initializers** for `RunOptions` configuration

## Platform Support

This fork focuses on **Windows** with MSVC. The POSIX process-spawning backend (`builder_posix.cpp`) is intentionally left unimplemented — all pipe utilities, environment handling, and type definitions are cross-platform, but `Popen`/`run()` only work on Windows.

**If you need POSIX/Linux/macOS support**, or don't require C++23, use the original repository: [benman64/subprocess](https://github.com/benman64/subprocess) (C++17, full POSIX support).

## Deviations from Python

- On Windows, `terminate()` sends `CTRL_BREAK_EVENT` instead of hard termination. Use `kill()` for `TerminateProcess`. Enable `soft_kill` mode to make `kill()` behave like `terminate()`.
- `cin`, `cout`, `cerr` are used instead of `stdin`, `stdout`, `stderr` since `std*` names are macros in C++.
- The `CTRL+C` signal (`PSIGINT`) affects the entire console process group on Windows. See the [Microsoft docs](https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent) for details.

## License

See the original [benman64/subprocess](https://github.com/benman64/subprocess) for license terms.
