# subproc::PipeOption

[Back to README](../README.md)

Enum controlling how stdin, stdout, and stderr are connected between parent and child processes.

## Definition

```cpp
enum class PipeOption : int {
    inherit,
    cout,
    cerr,
    specific,
    pipe,
    close,
    none
};
```

## Values

### `PipeOption::inherit`

Inherits the corresponding handle from the parent process. This is the default for all three streams.

- **stdin**: child reads from the same console/pipe as the parent.
- **stdout/stderr**: child writes to the same console/pipe as the parent.

```cpp
// Output appears directly on the parent's console
subproc::run({"echo", "hello"});  // inherit is the default
```

### `PipeOption::pipe`

Creates a new anonymous pipe connecting parent and child.

- **For stdin (`cin`)**: the parent can write to `Popen::cin`, or pass a `std::string` directly.
- **For stdout/stderr**: output is captured into `CompletedProcess::cout` / `CompletedProcess::cerr`.

```cpp
auto cp = subproc::run({"echo", "hello"}, {.cout = PipeOption::pipe});
// cp.cout == "hello\r\n"
```

### `PipeOption::cout`

Redirects stderr to the same destination as stdout. Equivalent to shell `2>&1`. **Only valid for the `cerr` field.**

```cpp
auto cp = subproc::run({"my_program"},
    {.cout = PipeOption::pipe, .cerr = PipeOption::cout});
// Both stdout and stderr are captured in cp.cout
```

### `PipeOption::cerr`

Redirects stdout to the same destination as stderr. **Only valid for the `cout` field.** This is the reverse of `PipeOption::cout`.

### `PipeOption::close`

Closes the stream descriptor. The child process receives EOF on stdin, and any writes to stdout/stderr are discarded (similar to redirecting to `/dev/null`).

```cpp
subproc::run({"noisy_tool"}, {.cout = PipeOption::close, .cerr = PipeOption::close});
```

### `PipeOption::none`

Sets the handle to `nullptr`. The child process has no file descriptor for this stream. Different from `close` — with `none`, the stream handle simply does not exist.

### `PipeOption::specific`

Indicates that a specific `PipeHandle` (Windows `HANDLE`) is being provided. You typically don't set this directly — it is inferred automatically when you pass a `PipeHandle` value to `cin`, `cout`, or `cerr` in `RunOptions` or `RunBuilder`.

```cpp
auto file_handle = subproc::pipe_file("output.txt", "w");
auto cp = subproc::run({"echo", "hello"}, {.cout = file_handle});
// Output is written to output.txt
subproc::pipe_close(file_handle);
```

## PipeVar — The Variant Type

`PipeOption` is one of several types that can be assigned to the `cin`, `cout`, and `cerr` fields. The full variant is:

```cpp
using PipeVar = std::variant<PipeOption, std::string, PipeHandle, std::istream*, std::ostream*, FILE*>;
```

The `get_pipe_option()` helper resolves which `PipeOption` a `PipeVar` represents:

| PipeVar value | Resolved PipeOption |
|---------------|---------------------|
| `PipeOption` value | The value itself |
| `PipeHandle` | `PipeOption::specific` |
| `std::string`, `std::istream*`, `std::ostream*`, `FILE*` | `PipeOption::pipe` |

## Python Comparison

| subproc | Python `subprocess` |
|---------|---------------------|
| `PipeOption::inherit` | Default behavior |
| `PipeOption::pipe` | `subprocess.PIPE` |
| `PipeOption::cout` | `subprocess.STDOUT` |
| `PipeOption::close` | `subprocess.DEVNULL` |
| `PipeOption::none` | *(no equivalent)* |
| `PipeOption::cerr` | *(no equivalent)* |
| `PipeOption::specific` | *(passing a file descriptor)* |

## See Also

- [`RunOptions`](run-options.md) — uses `PipeVar` for stream fields
- [`RunBuilder`](run-builder.md) — fluent setters accept `PipeVar`
- [Pipe Functions](pipe-functions.md) — low-level pipe operations
