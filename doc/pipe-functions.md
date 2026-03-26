# Pipe Functions

[Back to README](../README.md)

Low-level functions for creating, reading, writing, and managing anonymous pipes. These are the building blocks used internally by [`Popen`](popen.md) and are also available for direct use when you need fine-grained control over inter-process communication.

## Types

### PipePair

```cpp
struct PipePair {
    PipeHandle input = kBadPipeValue;    // read end
    PipeHandle output = kBadPipeValue;   // write end

    void close();          // close both ends
    void close_input();    // close read end
    void close_output();   // close write end
    void disown();         // release ownership without closing
    explicit operator bool() const noexcept;  // true if either end is valid
};
```

An RAII wrapper around a pair of pipe handles. The destructor calls `close()`. Move-only (no copy).

### PipeHandle

```cpp
using PipeHandle = HANDLE;
constexpr PipeHandle kBadPipeValue = nullptr;
```

A Windows `HANDLE` representing one end of a pipe.

## Functions

### `pipe_create(inheritable)`

```cpp
PipePair pipe_create(bool inheritable = true);
```

Creates a new anonymous pipe and returns both ends as a `PipePair`.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `inheritable` | `bool` | `true` | If `true`, child processes can inherit the handles. Set to `false` when connecting pipes between multiple child processes manually. |

**Throws**: `OSError` if `CreatePipe` fails.

```cpp
auto pp = subproc::pipe_create(false);
// pp.output → write end (feed data in)
// pp.input  → read end (read data out)
```

### `pipe_close(handle)`

```cpp
bool pipe_close(PipeHandle handle);
```

Closes a single pipe handle. Returns `true` on success, `false` if the handle was already invalid.

### `pipe_read(handle, buffer, size)`

```cpp
ssize_t pipe_read(PipeHandle handle, void* buffer, size_t size);
```

Reads up to `size` bytes from the pipe into `buffer`. **Blocks** if the pipe is empty and the write end is still open.

**Returns**: number of bytes read, or `-1` on error. Returns `0` at EOF (write end closed, no data remaining).

### `pipe_write(handle, buffer, size)`

```cpp
ssize_t pipe_write(PipeHandle handle, const void* buffer, size_t size);
```

Writes up to `size` bytes from `buffer` to the pipe. May write fewer bytes than requested (partial write).

**Returns**: number of bytes written, or `-1` on error.

### `pipe_read_all(handle)`

```cpp
std::string pipe_read_all(PipeHandle handle);
```

Reads all data from the pipe until EOF and returns it as a string. Blocks until the write end is closed.

```cpp
auto pp = subproc::pipe_create(false);
// ... write data to pp.output, then close pp.output ...
std::string data = subproc::pipe_read_all(pp.input);
```

### `pipe_write_fully(handle, buffer, size)`

```cpp
ssize_t pipe_write_fully(PipeHandle handle, const void* buffer, size_t size);
```

Writes the **entire** buffer, retrying on partial writes. Unlike `pipe_write()`, this guarantees all data is written (or an error occurs).

**Returns**: total bytes written. On error, returns `-(bytes_written_so_far + 1)` (negative value).

```cpp
std::string data = "hello world";
ssize_t n = subproc::pipe_write_fully(pp.output, data.c_str(), data.size());
// n == 11 on success
```

### `pipe_peek_bytes(handle)`

```cpp
ssize_t pipe_peek_bytes(PipeHandle handle);
```

Returns the number of bytes available for reading **without blocking or consuming** the data.

**Returns**: number of available bytes, or `-1` on error.

```cpp
ssize_t available = subproc::pipe_peek_bytes(pp.input);
if (available > 0) {
    // safe to read without blocking
}
```

### `pipe_read_some(handle, buffer, size)`

```cpp
ssize_t pipe_read_some(PipeHandle handle, void* buffer, size_t size);
```

Blocks until at least 1 byte is available, then reads all currently available data (up to `size` bytes). This is useful when you want to wait for data but read as much as possible in one call.

**Behavior**:
1. Reads 1 byte (blocking).
2. Peeks to see how much more is available.
3. Reads the remaining available data (non-blocking).

**Returns**: total bytes read, or `-1` on error.

### `pipe_wait_for_read(handle, seconds)`

```cpp
int pipe_wait_for_read(PipeHandle handle, double seconds);
```

Waits until data is available to read or the timeout expires.

| Return value | Meaning |
|--------------|---------|
| `1` | Data is available to read. |
| `0` | Timeout expired, no data available. |
| `-1` | Error occurred. |

```cpp
int status = subproc::pipe_wait_for_read(proc.cout, 5.0);
if (status == 1) {
    char buf[4096];
    ssize_t n = subproc::pipe_read(proc.cout, buf, sizeof(buf));
}
```

### `pipe_set_blocking(handle, should_block)`

```cpp
bool pipe_set_blocking(PipeHandle handle, bool should_block);
```

Toggles between blocking and non-blocking mode for a named pipe handle.

- `true` — reads block when no data is available (default).
- `false` — reads return immediately with 0 bytes if no data is available.

**Returns**: `true` on success.

**Note**: This uses `SetNamedPipeHandleState` internally, which only works on named pipes. Anonymous pipes created by `pipe_create()` may have limited support depending on Windows version.

### `pipe_set_inheritable(handle, inheritable)`

```cpp
void pipe_set_inheritable(PipeHandle handle, bool inheritable);
```

Controls whether a pipe handle is inherited by child processes.

**Throws**: `OSError` if `SetHandleInformation` fails, `std::invalid_argument` if the handle is invalid.

### `pipe_file(filename, mode)`

```cpp
PipeHandle pipe_file(std::string_view filename, std::string_view mode);
```

Opens a file and returns it as a `PipeHandle`, suitable for use as a subprocess stdin/stdout/stderr.

| Mode characters | Meaning |
|-----------------|---------|
| `"r"` | Open for reading (`OPEN_EXISTING`) |
| `"w"` | Open for writing (`CREATE_ALWAYS`) |
| `"r+"` or `"w+"` | Open for both reading and writing |

**Returns**: a valid `PipeHandle`, or `kBadPipeValue` if the file cannot be opened.

```cpp
// Use a file as stdin
auto input = subproc::pipe_file("input.txt", "r");
auto cp = subproc::run({"my_tool"}, {.cin = input, .cout = PipeOption::pipe});
subproc::pipe_close(input);

// Write stdout to a file
auto output = subproc::pipe_file("output.txt", "w");
subproc::run({"echo", "hello"}, {.cout = output});
subproc::pipe_close(output);
```

### `pipe_ignore_and_close(handle)`

```cpp
void pipe_ignore_and_close(PipeHandle handle);
```

Spawns a detached background thread that continuously reads from the pipe (discarding data) until EOF, then closes the handle. This prevents the child process from blocking if its output pipe buffer fills up.

## Piping Between Processes

A common pattern is connecting the stdout of one process to the stdin of another:

```cpp
auto pp = subproc::pipe_create(false);

auto writer = subproc::RunBuilder({"echo", "hello"})
    .cout(pp.output)    // writer's stdout → pipe write end
    .popen();

auto reader = subproc::RunBuilder({"cat"})
    .cin(pp.input)      // reader's stdin ← pipe read end
    .cout(PipeOption::pipe)
    .popen();

pp.close();  // parent doesn't need the pipe ends

auto result = subproc::run(reader);
// result.cout == "hello\r\n"

writer.close();
reader.close();
```

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `kBadPipeValue` | `nullptr` | Sentinel for an invalid/closed pipe handle. |
| `kPipeBufferSize` | `8192` | Default buffer size for pipe read/write operations. |

## See Also

- [`Popen`](popen.md) — uses pipe handles for subprocess I/O
- [`PipeOption`](pipe-option.md) — high-level pipe configuration
- [`run()`](run.md) — manages pipes automatically
