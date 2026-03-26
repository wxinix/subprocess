# subproc::CompletedProcess

[Back to README](../README.md)

Represents the result of a completed subprocess. Returned by [`subproc::run()`](run.md) and [`RunBuilder::run()`](run-builder.md).

## Definition

```cpp
struct CompletedProcess {
    CommandLine args;
    int64_t returncode = -1;
    std::string cout;
    std::string cerr;
    explicit operator bool() const;
};
```

## Fields

### `args`

| Type | Description |
|------|-------------|
| `CommandLine` (`std::vector<std::string>`) | The command line that was executed. |

### `returncode`

| Type | Description |
|------|-------------|
| `int64_t` | The exit code of the process. `0` typically means success. Negative values may indicate the process was killed by a signal. |

### `cout`

| Type | Description |
|------|-------------|
| `std::string` | Captured standard output. Empty if stdout was not piped (i.e., `PipeOption::pipe` was not set for `cout`). |

On Windows, line endings in the output are typically `\r\n`.

### `cerr`

| Type | Description |
|------|-------------|
| `std::string` | Captured standard error. Empty if stderr was not piped. |

If `cerr` was set to `PipeOption::cout`, stderr content is merged into `cout` and this field remains empty.

### `operator bool()`

Returns `true` if `returncode == 0` (success). Allows idiomatic success checking:

```cpp
auto cp = subproc::run({"my_program"}, {.cout = PipeOption::pipe});
if (cp) {
    std::println("Success: {}", cp.cout);
} else {
    std::println("Failed with code {}", cp.returncode);
}
```

## Examples

### Capturing stdout

```cpp
auto cp = subproc::run({"echo", "hello"}, {.cout = PipeOption::pipe});
// cp.cout == "hello\r\n"
// cp.returncode == 0
// cp.cerr == ""
// static_cast<bool>(cp) == true
```

### Capturing both streams

```cpp
auto cp = subproc::run({"my_program"},
    {.cout = PipeOption::pipe, .cerr = PipeOption::pipe});
std::println("stdout: {}", cp.cout);
std::println("stderr: {}", cp.cerr);
```

### Checking success

```cpp
if (auto cp = subproc::run({"compiler", "main.cpp"}, {.cout = PipeOption::pipe})) {
    std::println("Compiled successfully");
} else {
    std::println("Compilation failed (exit code {})", cp.returncode);
}
```

## See Also

- [`run()`](run.md) — returns `CompletedProcess`
- [`RunBuilder`](run-builder.md) — `RunBuilder::run()` returns `CompletedProcess`
- [`CalledProcessError`](exceptions.md#calledprocesserror) — thrown instead of returning on failure when `raise_on_nonzero` is set
