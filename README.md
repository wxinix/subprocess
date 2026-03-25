# subproc

A modern C++23 **single-header** subprocess library for Windows, inspired by Python's `subprocess` module.

**Just one file** — drop [`subproc.hpp`](subproc/subproc.hpp) into your project and `#include` it. No `.cpp` files, no build steps, no linking required.

```cpp
#include <subproc.hpp>
```

Maintained by [@wxinix](https://github.com/wxinix).

## Features

- **Single-header, Windows-native** — one file, zero dependencies beyond the C++23 standard library and Win32 API
- **Python-like API** with C++20 designated initializers and C++23 deducing this
- **Fluent builder pattern** via `RunBuilder` with perfect-forwarding method chains
- **Pipe management** connecting stdout of process A to stdin of process B
- **Environment utilities** with `subproc::cenv["VAR"] = "value"` syntax
- **RAII guards** for environment (`EnvGuard`) and working directory (`CwdGuard`)
- **`find_program`** with caching and Python 3 detection
- **Stream redirection** to C++ streams (`std::istream*`, `std::ostream*`), `FILE*`, or `std::string`
- **Signal handling** with `send_signal()`, `terminate()`, `kill()`, and soft-kill mode
- **Timeout support** via `try_wait()` (returns `std::optional`) and `wait()` (throws `TimeoutExpired`)

## Requirements

- **Windows** with MSVC 19.37+ (Visual Studio 2022 17.7+)
- **C++23** (`/std:c++latest` or `/std:c++23`)
- **CMake 3.27+** (if using the provided build system; otherwise just copy the header)

## Installation

Copy `subproc/subproc.hpp` into your project's include path. That's it.

Or, if using CMake as a subdirectory:

```cmake
add_subdirectory(subproc)               # points to this repo
target_link_libraries(your_target PRIVATE subproc)
```

## Building Tests

```bash
cmake --preset debug
cmake --build build/debug
build/debug/test/Debug/basic_test.exe --verbose
```

## Quick Start

### Running a Command

```cpp
subproc::run({"echo", "hello", "world"});
```

### Capturing Output

```cpp
// Designated initializers
auto cp = subproc::run({"echo", "hello"}, {.cout = PipeOption::pipe});
// cp.cout == "hello\r\n", cp.returncode == 0

// Fluent RunBuilder
auto cp2 = subproc::RunBuilder({"echo", "hello"})
    .cout(PipeOption::pipe)
    .run();
```

### Capturing Both stdout and stderr

```cpp
auto cp = subproc::run({"my_program"},
    {.cout = PipeOption::pipe, .cerr = PipeOption::pipe});

// Merge stderr into stdout (like shell 2>&1)
auto cp2 = subproc::run({"my_program"},
    {.cout = PipeOption::pipe, .cerr = PipeOption::cout});
```

### Sending Data to stdin

```cpp
auto cp = subproc::run({"cat"},
    subproc::RunBuilder().cin("piped data").cout(PipeOption::pipe));
// cp.cout == "piped data"
```

### Checking for Errors

```cpp
try {
    auto cp = subproc::run({"false"}, {.raise_on_nonzero = true});
} catch (const subproc::CalledProcessError& e) {
    std::println("Command failed with code {}", e.returncode);
}

// Or inspect the return code directly
auto cp = subproc::run({"my_program"}, {.cout = PipeOption::pipe});
if (cp) { /* success */ }
```

### Timeouts

```cpp
try {
    subproc::run({"sleep", "30"}, {.timeout = 5.0});
} catch (const subproc::TimeoutExpired& e) {
    // e.timeout == 5.0
}
```

### Custom Working Directory and Environment

```cpp
auto cp = subproc::run({"dir"}, {.cout = PipeOption::pipe, .cwd = "C:/temp"});

auto env = subproc::current_env_copy();
env["MY_VAR"] = "my_value";
auto cp2 = subproc::run({"printenv", "MY_VAR"}, {.cout = PipeOption::pipe, .env = env});
```

### Asynchronous Control with Popen

```cpp
auto popen = subproc::RunBuilder({"long_running_cmd"})
    .cout(PipeOption::pipe)
    .new_process_group(true)
    .popen();

while (!popen.poll()) { /* do other work */ }
std::println("exit code: {}", popen.returncode);

// Or wait with timeout (no exceptions)
if (auto rc = popen.try_wait(5.0)) {
    std::println("exited with {}", *rc);
} else {
    popen.terminate();
    popen.wait();
}
popen.close();
```

### Piping Between Processes

```cpp
auto pp = subproc::pipe_create(false);
auto writer = subproc::RunBuilder({"echo", "hello"}).cout(pp.output).popen();
auto reader = subproc::RunBuilder({"cat"}).cin(pp.input).cout(PipeOption::pipe).popen();
pp.close();
auto result = subproc::run(reader);  // result.cout == "hello\r\n"
writer.close(); reader.close();
```

### Environment Management

```cpp
subproc::cenv["MY_VAR"] = "value";    // set
subproc::cenv["COUNT"] = 42;          // int
subproc::cenv["DEBUG"] = true;        // bool -> "1"
subproc::cenv["MY_VAR"] = nullptr;    // delete
if (subproc::cenv["MY_VAR"]) { /* exists */ }

{
    subproc::EnvGuard guard;           // snapshot env + cwd
    subproc::cenv["PATH"] = "C:/custom";
    // ... run subprocesses ...
}  // auto-restored
```

### Windows-Specific Options

```cpp
auto cp = subproc::run({"background_tool"},
    {.create_no_window = true,
     .detached_process = true,
     .new_process_group = true});
```

### Signal Handling

```cpp
auto popen = subproc::RunBuilder({"server"}).new_process_group(true).popen();
popen.send_signal(subproc::SigNum::PSIGTERM);  // CTRL_BREAK_EVENT
popen.terminate();                               // same as PSIGTERM
popen.kill();                                    // TerminateProcess

popen.enable_soft_kill(true);
popen.kill();  // now sends CTRL_BREAK instead of TerminateProcess
```

### Advanced Pipe Operations

```cpp
auto pp = subproc::pipe_create(false);

ssize_t available = subproc::pipe_peek_bytes(pp.input);
char buf[4096];
ssize_t n = subproc::pipe_read_some(pp.input, buf, sizeof(buf));
ssize_t written = subproc::pipe_write_fully(pp.output, data.c_str(), data.size());
int status = subproc::pipe_wait_for_read(pp.input, 5.0);  // 1=ready, 0=timeout, -1=error
subproc::pipe_set_blocking(pp.input, false);

// File as pipe handle
auto h = subproc::pipe_file("input.txt", "r");
auto cp = subproc::run({"tool"}, subproc::RunBuilder().cin(h).cout(PipeOption::pipe));
(void)subproc::pipe_close(h);
```

## API Reference

### `subproc::run()`

```cpp
CompletedProcess run(CommandLine command, const RunOptions& options = {});
CompletedProcess run(Popen& popen, bool check = false);
```

### `RunOptions`

| Field               | Type          | Default   | Description                                |
|---------------------|---------------|-----------|--------------------------------------------|
| `cin`               | `PipeVar`     | `inherit` | stdin: `PipeOption`, `string`, handle, etc. |
| `cout`              | `PipeVar`     | `inherit` | stdout redirection                         |
| `cerr`              | `PipeVar`     | `inherit` | stderr redirection                         |
| `cwd`               | `std::string` | `""`      | Working directory                          |
| `env`               | `EnvMap`      | `{}`      | Environment variables (empty = inherit)    |
| `timeout`           | `double`      | `-1`      | Timeout in seconds (-1 = none)             |
| `raise_on_nonzero`  | `bool`        | `false`   | Throw `CalledProcessError` on failure      |
| `new_process_group` | `bool`        | `false`   | Create a new process group                 |
| `create_no_window`  | `bool`        | `false`   | No console window                          |
| `detached_process`  | `bool`        | `false`   | Detached from parent console               |

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
| `terminate()`         | `CTRL_BREAK_EVENT` (graceful)                          |
| `kill()`              | `TerminateProcess` (hard kill)                         |
| `close()`             | Closes all pipes, waits for exit, cleans up handles    |
| `close_cin()`         | Closes stdin pipe (signals EOF to child)               |
| `enable_soft_kill(b)` | If true, `kill()` sends `CTRL_BREAK` instead           |

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
| `pipe_set_inheritable(handle, inherit)` | Controls child handle inheritance |
| `pipe_file(filename, mode)` | Opens a file as a pipe handle |
| `pipe_ignore_and_close(handle)` | Spawns a thread to drain and close the pipe |

### Exception Hierarchy

```
std::runtime_error
  └── SubprocError
        ├── OSError
        │     └── SpawnError
        ├── CommandNotFoundError
        ├── CalledProcessError  (returncode, cmd, cout, cerr)
        └── TimeoutExpired      (timeout, cmd, cout, cerr)
```

## C++23 Features Used

- **Deducing this** for `RunBuilder` method chains
- **`std::visit` with `overloaded`** pattern for variant dispatch
- **`std::string::contains()`** and **`starts_with()`**
- **`std::optional`** for `try_wait()` — no-throw timeout handling
- **`std::print` / `std::println`** in test code
- **`std::ranges::to`**, **`std::ranges::replace`**, **`std::ranges::any_of`**
- **`using enum`** for cleaner switch statements
- **Concepts and `requires` clauses** for template constraints
- **Designated initializers** for `RunOptions` configuration

## Deviations from Python

- `terminate()` sends `CTRL_BREAK_EVENT`; use `kill()` for `TerminateProcess`. Enable `soft_kill` to make `kill()` behave like `terminate()`.
- `cin`, `cout`, `cerr` are used instead of `stdin`, `stdout`, `stderr` (C++ macro conflict).
- `CTRL+C` (`PSIGINT`) affects the entire console process group. See [Microsoft docs](https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent).

## Acknowledgments

This library is inspired by [benman64/subprocess](https://github.com/benman64/subprocess), which provides a C++17 cross-platform subprocess library with full POSIX support. The API design philosophy and Python-like ergonomics originate from that project.

## License

MIT License. See [LICENSE](LICENSE) for details.
