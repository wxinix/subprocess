# subproc::RunBuilder

[Back to README](../README.md)

A fluent builder for configuring and launching subprocesses. `RunBuilder` provides method chaining as an alternative to C++20 designated initializers on [`RunOptions`](run-options.md).

## Class Definition

```cpp
struct RunBuilder {
    RunOptions options{};
    CommandLine command{};

    RunBuilder() = default;
    RunBuilder(CommandLine cmd);
    RunBuilder(std::initializer_list<std::string> cmd);

    // Fluent setters (each returns *this for chaining)
    auto&& cin(const PipeVar& v);
    auto&& cout(const PipeVar& v);
    auto&& cerr(const PipeVar& v);
    auto&& cwd(const std::string& v);
    auto&& env(const EnvMap& v);
    auto&& timeout(double v);
    auto&& raise_on_nonzero(bool v);
    auto&& new_process_group(bool v);
    auto&& create_no_window(bool v);
    auto&& detached_process(bool v);

    // Terminal operations
    operator RunOptions() const;          // implicit conversion
    CompletedProcess run() const;         // run and wait
    Popen popen() const;                  // launch async
};
```

## Constructors

| Constructor | Description |
|-------------|-------------|
| `RunBuilder()` | Creates an empty builder. The command must be passed to `run()` separately, or this builder is used purely as an `RunOptions` factory (via implicit conversion). |
| `RunBuilder(CommandLine cmd)` | Creates a builder with the given command line. |
| `RunBuilder({"echo", "hello"})` | Creates a builder from an initializer list. |

## Fluent Setters

All setters use C++23 deducing `this`, so they work correctly with both lvalues and rvalues — you can chain on temporaries without copies.

| Method | Corresponding `RunOptions` field | Description |
|--------|----------------------------------|-------------|
| `.cin(v)` | `cin` | Set stdin source: `PipeOption`, `std::string`, `PipeHandle`, `std::istream*`, `FILE*` |
| `.cout(v)` | `cout` | Set stdout destination: `PipeOption`, `PipeHandle`, `std::ostream*`, `FILE*` |
| `.cerr(v)` | `cerr` | Set stderr destination: `PipeOption`, `PipeHandle`, `std::ostream*`, `FILE*` |
| `.cwd(v)` | `cwd` | Set working directory for the child process |
| `.env(v)` | `env` | Set environment variables (replaces inherited environment) |
| `.timeout(v)` | `timeout` | Set timeout in seconds (-1 = no timeout) |
| `.raise_on_nonzero(v)` | `raise_on_nonzero` | If `true`, throw `CalledProcessError` on non-zero exit |
| `.new_process_group(v)` | `new_process_group` | Create process in a new process group |
| `.create_no_window(v)` | `create_no_window` | Do not create a console window |
| `.detached_process(v)` | `detached_process` | Detach from parent console |

## Terminal Operations

### `run()`

Calls `subproc::run(command, options)` — launches the subprocess, waits for it to complete, and returns a [`CompletedProcess`](completed-process.md).

```cpp
auto cp = subproc::RunBuilder({"echo", "hello"})
    .cout(PipeOption::pipe)
    .run();
// cp.cout == "hello\r\n"
```

### `popen()`

Calls `Popen(command, options)` — launches the subprocess but does **not** wait. Returns a [`Popen`](popen.md) for asynchronous control.

```cpp
auto proc = subproc::RunBuilder({"server", "--port", "8080"})
    .cout(PipeOption::pipe)
    .new_process_group(true)
    .popen();

// ... do other work ...
proc.terminate();
proc.wait();
proc.close();
```

### `operator RunOptions()`

Implicitly converts to `RunOptions`, so you can pass a `RunBuilder` anywhere a `RunOptions` is expected:

```cpp
auto cp = subproc::run({"cat"},
    subproc::RunBuilder().cin("hello").cout(PipeOption::pipe));
```

## Full Example

```cpp
// Fluent style with method chaining
auto result = subproc::RunBuilder({"grep", "-i", "error"})
    .cin("INFO: ok\nERROR: something failed\nDEBUG: trace\n")
    .cout(PipeOption::pipe)
    .cerr(PipeOption::pipe)
    .timeout(5.0)
    .raise_on_nonzero(false)
    .run();

std::println("Matches: {}", result.cout);
```

## See Also

- [`RunOptions`](run-options.md) — the struct `RunBuilder` wraps
- [`run()`](run.md) — the function `RunBuilder::run()` delegates to
- [`Popen`](popen.md) — returned by `RunBuilder::popen()`
- [`PipeOption`](pipe-option.md) — pipe redirection values
