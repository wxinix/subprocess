# Utility Functions

[Back to README](../README.md)

Miscellaneous helper functions for paths, shell escaping, timing, and string conversion.

## Path Utilities

### `abspath(dir, relative)`

```cpp
std::string abspath(std::string dir, std::string relative = "");
```

Converts a path to an absolute path. If `dir` is already absolute, returns it as-is. Otherwise resolves it relative to `relative` (or the current working directory if `relative` is empty).

```cpp
std::string abs = subproc::abspath("src/main.cpp");
// e.g., "C:/projects/my_app/src/main.cpp"

std::string abs2 = subproc::abspath("lib", "C:/projects");
// "C:/projects/lib"
```

## Shell Utilities

### `escape_shell_arg(arg, escape)`

```cpp
std::string escape_shell_arg(const std::string& arg, bool escape = true);
```

Escapes a shell argument for safe inclusion in a command line. Wraps the argument in double quotes and escapes internal `"` and `\` characters. Only quotes when necessary (when the argument contains special characters).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `arg` | `const std::string&` | — | The argument to escape. |
| `escape` | `bool` | `true` | If `false`, returns `arg` unchanged. |

Characters that trigger quoting: anything that is not alphanumeric, `.`, `_`, `-`, `+`, or `/`.

```cpp
subproc::escape_shell_arg("hello");           // "hello" (no quoting needed)
subproc::escape_shell_arg("hello world");     // "\"hello world\""
subproc::escape_shell_arg("--flag=value");    // "\"--flag=value\""
subproc::escape_shell_arg("arg", false);      // "arg" (escaping disabled)
```

## Timing Utilities

### `sleep_seconds(seconds)`

```cpp
double sleep_seconds(double seconds);
```

Sleeps for the specified duration. Returns the actual elapsed time in seconds (may be slightly more than requested due to scheduling).

```cpp
double elapsed = subproc::sleep_seconds(0.5);  // sleep 500ms
```

### `StopWatch`

```cpp
class StopWatch {
public:
    StopWatch();                    // starts automatically
    void start();                   // restart the timer
    double seconds() const;         // elapsed time since start
};
```

A simple monotonic timer for measuring elapsed time.

```cpp
subproc::StopWatch watch;
// ... do work ...
std::println("Took {} seconds", watch.seconds());
```

## String Conversion

### `utf8_to_utf16(input)`

```cpp
std::u16string utf8_to_utf16(const std::string& input);
```

Converts a UTF-8 string to UTF-16. Used internally for Windows API calls that require wide strings.

### `utf16_to_utf8(input)`

```cpp
std::string utf16_to_utf8(const std::u16string& input);
std::string utf16_to_utf8(const std::wstring& input);
```

Converts a UTF-16 string to UTF-8. Has overloads for both `std::u16string` and `std::wstring`.

## Types and Constants

| Name | Type | Value | Description |
|------|------|-------|-------------|
| `ssize_t` | `intptr_t` | — | Signed size type (POSIX compatibility). |
| `PipeHandle` | `HANDLE` | — | Windows handle for pipe operations. |
| `pid_t` | `DWORD` | — | Windows process ID type. |
| `CommandLine` | `std::vector<std::string>` | — | Command and arguments. |
| `EnvMap` | `std::map<std::string, std::string>` | — | Environment variable map. |
| `kPathDelimiter` | `char` | `';'` | PATH separator (`;` on Windows). |
| `kBadPipeValue` | `PipeHandle` | `nullptr` | Sentinel for invalid pipe handles. |
| `kBadReturnCode` | `int` | `-1000` | Sentinel for "not yet exited" return code. |
| `kPipeBufferSize` | `size_t` | `8192` | Default pipe I/O buffer size. |

## See Also

- [Environment Management](environment.md) — `getenv()`, `get_cwd()`, `set_cwd()`
- [Pipe Functions](pipe-functions.md) — uses `PipeHandle` and `kBadPipeValue`
