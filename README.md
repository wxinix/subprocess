# subproc

A modern C++23 **single-header** subprocess library for Windows, inspired by Python's `subprocess` module.  

To use it, just drop [`subproc.hpp`](subproc/subproc.hpp) into your project and `#include` it. No `.cpp` files, no build steps, no linking required.

```cpp
#include <subproc.hpp>
```

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

The simplest usage — launch a program and let its output go directly to the console (inheriting the parent's stdout). The argument to `run()` is a `CommandLine` (`std::vector<std::string>`) where the first element (`"echo"`) is the program to execute, and the remaining elements (`"hello"`, `"world"`) are passed as its arguments.

```cpp
subproc::run({"echo", "hello", "world"});
// "echo" is the program, "hello" and "world" are its arguments
// Console output: hello world
```

### Capturing Output

To capture a command's output as a string instead of printing it, set `.cout = PipeOption::pipe`. This creates an anonymous pipe between parent and child, collecting everything the child writes to stdout into `CompletedProcess::cout`.

```cpp
// Using C++20 designated initializers
auto cp = subproc::run({"echo", "hello"}, {.cout = PipeOption::pipe});
// cp.cout == "hello\r\n", cp.returncode == 0

// Using the fluent RunBuilder — same result, different syntax
auto cp2 = subproc::RunBuilder({"echo", "hello"})
    .cout(PipeOption::pipe)
    .run();
```

### Capturing Both stdout and stderr

Pipe both streams separately to inspect them independently. Or merge stderr into stdout with `PipeOption::cout` (equivalent to shell `2>&1`) so all output lands in `cp.cout`.

```cpp
// Separate capture — stdout and stderr in their own fields
auto cp = subproc::run({"my_program"},
    {.cout = PipeOption::pipe, .cerr = PipeOption::pipe});
// cp.cout has stdout, cp.cerr has stderr

// Merge stderr into stdout (like shell 2>&1)
auto cp2 = subproc::run({"my_program"},
    {.cout = PipeOption::pipe, .cerr = PipeOption::cout});
// cp2.cout has both stdout and stderr combined
```

### Sending Data to stdin

Pass a `std::string` to `cin` via `RunBuilder` to feed data into the child's standard input. The library writes the string to a pipe connected to the child's stdin, then closes the pipe (signaling EOF) so the child knows input is complete.

```cpp
auto cp = subproc::run({"cat"},
    subproc::RunBuilder().cin("piped data").cout(PipeOption::pipe));
// cp.cout == "piped data"
```

### Checking for Errors

By default, `run()` does not throw on non-zero exit codes — it just sets `returncode`. Set `raise_on_nonzero = true` to throw a `CalledProcessError` instead, which includes the exit code and any captured output. Alternatively, use the bool conversion operator to check success.

```cpp
// Throw on failure
try {
    auto cp = subproc::run({"false"}, {.raise_on_nonzero = true});
} catch (const subproc::CalledProcessError& e) {
    std::println("Command failed with code {}", e.returncode);
}

// Or inspect the return code manually
auto cp = subproc::run({"my_program"}, {.cout = PipeOption::pipe});
if (cp) { /* returncode == 0, success */ }
```

### Timeouts

Set `timeout` (in seconds) to limit how long `run()` waits. If the process doesn't finish in time, `run()` terminates it and throws `TimeoutExpired`. The exception carries the timeout value and any output captured before the timeout.

```cpp
try {
    subproc::run({"sleep", "30"}, {.timeout = 5.0});
} catch (const subproc::TimeoutExpired& e) {
    // e.timeout == 5.0, process has been terminated
}
```

### Custom Working Directory and Environment

Set `cwd` to run the child in a different directory. Pass a custom `env` map to replace the child's entire environment — use `current_env_copy()` to start from the current environment and then modify it, rather than building one from scratch.

```cpp
// Run in a specific directory
auto cp = subproc::run({"dir"}, {.cout = PipeOption::pipe, .cwd = "C:/temp"});

// Run with a modified environment
auto env = subproc::current_env_copy();
env["MY_VAR"] = "my_value";
auto cp2 = subproc::run({"printenv", "MY_VAR"}, {.cout = PipeOption::pipe, .env = env});
```

### Asynchronous Control with Popen

Use `popen()` instead of `run()` when you need to interact with the process while it's running. `Popen` gives you non-blocking `poll()`, timeout-safe `try_wait()`, and signal control. Always call `close()` when done to release handles.

```cpp
auto popen = subproc::RunBuilder({"long_running_cmd"})
    .cout(PipeOption::pipe)
    .new_process_group(true)    // required for targeted signal delivery
    .popen();

// Non-blocking check: do other work while the process runs
while (!popen.poll()) { /* do other work */ }
std::println("exit code: {}", popen.returncode);

// Or wait with a timeout (no exceptions — returns nullopt on timeout)
if (auto rc = popen.try_wait(5.0)) {
    std::println("exited with {}", *rc);
} else {
    popen.terminate();  // graceful shutdown via CTRL_BREAK_EVENT
    popen.wait();
}
popen.close();
```

### Piping Between Processes

Connect the stdout of one process to the stdin of another using a manually created pipe. Create a non-inheritable pipe, pass the write end as one process's stdout and the read end as another's stdin, then close the parent's copies.

```cpp
auto pp = subproc::pipe_create(false);  // non-inheritable pipe pair
auto writer = subproc::RunBuilder({"echo", "hello"}).cout(pp.output).popen();
auto reader = subproc::RunBuilder({"cat"}).cin(pp.input).cout(PipeOption::pipe).popen();
pp.close();  // parent doesn't need these ends anymore

auto result = subproc::run(reader);  // collect reader's output
// result.cout == "hello\r\n"
writer.close(); reader.close();
```

### Environment Management

`subproc::cenv` provides dictionary-style access to the current process's environment. It supports strings, integers, booleans, and `nullptr` (to delete). `EnvGuard` snapshots the environment and working directory on construction and restores both when it goes out of scope.

```cpp
subproc::cenv["MY_VAR"] = "value";    // set a string
subproc::cenv["COUNT"] = 42;          // int → "42"
subproc::cenv["DEBUG"] = true;        // bool → "1"
subproc::cenv["MY_VAR"] = nullptr;    // delete the variable
if (subproc::cenv["MY_VAR"]) { /* exists and non-empty */ }

{
    subproc::EnvGuard guard;           // snapshot env + cwd
    subproc::cenv["PATH"] = "C:/custom";
    // ... run subprocesses with modified environment ...
}  // environment and cwd automatically restored
```

### Windows-Specific Options

Control Windows process creation flags. `create_no_window` suppresses the console window (useful for background tasks). `detached_process` detaches the child from the parent console. `new_process_group` is required for sending targeted signals via `terminate()`.

```cpp
auto cp = subproc::run({"background_tool"},
    {.create_no_window = true,
     .detached_process = true,
     .new_process_group = true});
```

### Signal Handling

On Windows, `terminate()` sends `CTRL_BREAK_EVENT` (graceful — the child can handle it), while `kill()` calls `TerminateProcess` (immediate, cannot be caught). Enable soft kill to make `kill()` behave like `terminate()`, giving the child a chance to clean up.

```cpp
auto popen = subproc::RunBuilder({"server"}).new_process_group(true).popen();
popen.send_signal(subproc::SigNum::PSIGTERM);  // CTRL_BREAK_EVENT
popen.terminate();                               // same as PSIGTERM
popen.kill();                                    // TerminateProcess (hard kill)

popen.enable_soft_kill(true);
popen.kill();  // now sends CTRL_BREAK instead of TerminateProcess
```

### Advanced Pipe Operations

Low-level pipe functions for when you need fine-grained control over I/O. These let you peek at available data, do partial reads, wait for data with a timeout, toggle blocking mode, and use files as pipe handles.

```cpp
auto pp = subproc::pipe_create(false);

ssize_t available = subproc::pipe_peek_bytes(pp.input);       // check without consuming
char buf[4096];
ssize_t n = subproc::pipe_read_some(pp.input, buf, sizeof(buf)); // block for 1 byte, drain rest
ssize_t written = subproc::pipe_write_fully(pp.output, data.c_str(), data.size()); // write all
int status = subproc::pipe_wait_for_read(pp.input, 5.0);     // 1=ready, 0=timeout, -1=error
subproc::pipe_set_blocking(pp.input, false);                   // switch to non-blocking

// Use a file as a pipe handle for subprocess I/O
auto h = subproc::pipe_file("input.txt", "r");
auto cp = subproc::run({"tool"}, subproc::RunBuilder().cin(h).cout(PipeOption::pipe));
(void)subproc::pipe_close(h);
```

## API Reference

Detailed documentation for each API component:

| Document | Description |
|----------|-------------|
| [`run()`](doc/run.md) | Primary entry point — run a command and collect results |
| [`RunBuilder`](doc/run-builder.md) | Fluent builder for configuring and launching subprocesses |
| [`RunOptions`](doc/run-options.md) | Configuration struct for `run()` (stdin/stdout/stderr, timeout, env, cwd) |
| [`PipeOption`](doc/pipe-option.md) | Enum controlling stream redirection between parent and child |
| [`CompletedProcess`](doc/completed-process.md) | Result struct returned by `run()` (exit code, captured output) |
| [`Popen`](doc/popen.md) | Asynchronous process control (poll, wait, signal, pipe I/O) |
| [Pipe Functions](doc/pipe-functions.md) | Low-level pipe creation, reading, writing, and management |
| [Environment Management](doc/environment.md) | `cenv`, `EnvGuard`, `CwdGuard`, `current_env_copy()` |
| [Exception Hierarchy](doc/exceptions.md) | `SubprocError`, `CalledProcessError`, `TimeoutExpired`, etc. |
| [`find_program()`](doc/find-program.md) | Locate executables on PATH with Python 3 detection |
| [Signal Handling](doc/signals.md) | Windows signal mapping, soft kill, process groups |
| [Utility Functions](doc/utilities.md) | Paths, shell escaping, timing, string conversion, constants |

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
