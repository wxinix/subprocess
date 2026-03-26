# Exception Hierarchy

[Back to README](../README.md)

All exceptions in subproc inherit from `SubprocError`, which itself inherits from `std::runtime_error`. This lets you catch all subproc errors with a single handler while also allowing fine-grained handling of specific error types.

## Hierarchy

```
std::runtime_error
  └── SubprocError
        ├── OSError
        │     └── SpawnError
        ├── CommandNotFoundError
        ├── CalledProcessError
        └── TimeoutExpired
```

## SubprocError

```cpp
struct SubprocError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
```

Base class for all subproc exceptions. Catch this to handle any error from the library.

```cpp
try {
    subproc::run({"some_command"}, {.raise_on_nonzero = true});
} catch (const subproc::SubprocError& e) {
    std::println(stderr, "subproc error: {}", e.what());
}
```

## OSError

```cpp
struct OSError : SubprocError {
    using SubprocError::SubprocError;
};
```

Thrown when a Win32 API call fails (e.g., `CreatePipe`, `WaitForSingleObject`, `SetHandleInformation`). The `what()` message includes the Windows error code and/or description.

## SpawnError

```cpp
struct SpawnError : OSError {
    using OSError::OSError;
};
```

A specialization of `OSError` thrown specifically when `CreateProcess` fails. This indicates the process could not be started (e.g., access denied, invalid executable format).

```cpp
try {
    subproc::run({"my_program"});
} catch (const subproc::SpawnError& e) {
    std::println(stderr, "Failed to start process: {}", e.what());
}
```

## CommandNotFoundError

```cpp
struct CommandNotFoundError : SubprocError {
    using SubprocError::SubprocError;
};
```

Thrown when the program specified in `command[0]` cannot be found on `PATH` or as an absolute/relative path. Checked before `CreateProcess` is called.

```cpp
try {
    subproc::run({"nonexistent_program"});
} catch (const subproc::CommandNotFoundError& e) {
    std::println(stderr, "{}", e.what());
    // "Command "nonexistent_program" not found."
}
```

## CalledProcessError

```cpp
struct CalledProcessError : SubprocError {
    CalledProcessError(const std::string& msg, CommandLine cmd, int64_t retcode,
                       std::string cout, std::string cerr);

    int64_t returncode;
    CommandLine cmd;
    std::string cout;
    std::string cerr;
};
```

Thrown by `run()` when `raise_on_nonzero = true` and the process exits with a non-zero return code.

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `returncode` | `int64_t` | The non-zero exit code. |
| `cmd` | `CommandLine` | The command that was executed. |
| `cout` | `std::string` | Captured stdout (if piped). |
| `cerr` | `std::string` | Captured stderr (if piped). |

### Example

```cpp
try {
    auto cp = subproc::run({"false"},
        {.cout = PipeOption::pipe, .cerr = PipeOption::pipe, .raise_on_nonzero = true});
} catch (const subproc::CalledProcessError& e) {
    std::println("Command failed with code {}", e.returncode);
    std::println("stdout: {}", e.cout);
    std::println("stderr: {}", e.cerr);
}
```

## TimeoutExpired

```cpp
struct TimeoutExpired : SubprocError {
    TimeoutExpired(const std::string& msg, CommandLine cmd, double timeout,
                   std::string cout, std::string cerr);

    CommandLine cmd;
    double timeout;
    std::string cout;
    std::string cerr;
};
```

Thrown when a process does not complete within the specified timeout.

- In `run()`: thrown after the process is terminated.
- In `Popen::wait()`: thrown when the timeout expires (the process is **not** automatically terminated).

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `cmd` | `CommandLine` | The command that timed out. |
| `timeout` | `double` | The timeout value in seconds. |
| `cout` | `std::string` | Any stdout captured before the timeout. |
| `cerr` | `std::string` | Any stderr captured before the timeout. |

### Example

```cpp
try {
    subproc::run({"sleep", "60"}, {.timeout = 5.0});
} catch (const subproc::TimeoutExpired& e) {
    std::println("Timed out after {} seconds", e.timeout);
}
```

## Recommended Catch Order

Catch from most specific to least specific:

```cpp
try {
    subproc::run(cmd, options);
} catch (const subproc::TimeoutExpired& e) {
    // handle timeout
} catch (const subproc::CalledProcessError& e) {
    // handle non-zero exit
} catch (const subproc::CommandNotFoundError& e) {
    // handle missing program
} catch (const subproc::SpawnError& e) {
    // handle CreateProcess failure
} catch (const subproc::SubprocError& e) {
    // catch-all for any subproc error
}
```

## See Also

- [`run()`](run.md) — throws `TimeoutExpired` and `CalledProcessError`
- [`Popen`](popen.md) — `wait()` throws `TimeoutExpired`
- [Pipe Functions](pipe-functions.md) — `pipe_create()` throws `OSError`
