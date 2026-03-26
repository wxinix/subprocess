# subproc::find_program()

[Back to README](../README.md)

Locates an executable on the system `PATH`, with special handling for Python 3.

## Signature

```cpp
std::string find_program(const std::string& name);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `const std::string&` | The program name (e.g., `"git"`, `"python3"`) or a path (absolute or relative). |

## Return Value

Returns the absolute path to the program if found, or an empty string if not found.

## Behavior

### Standard program lookup

1. If `name` is an absolute path, relative path (`./...`), or starts with `/`, checks the path directly (with `PATHEXT` extensions).
2. Checks the internal cache for a previous lookup of `name`.
3. Searches each directory in the `PATH` environment variable, trying the name with each `PATHEXT` extension (e.g., `.exe`, `.cmd`, `.bat`).

### Python 3 special case

When `name` is `"python3"`:
1. Looks up `"python"` on `PATH`.
2. If found, runs `python --version` and checks if the output contains `"3."`.
3. Returns the path only if it is Python 3; returns empty string otherwise.

This handles the common Windows case where Python 3 is installed as `python.exe` rather than `python3.exe`.

## Caching

Results are cached in a process-wide, mutex-protected map. Call `find_program_clear_cache()` to reset the cache (e.g., after modifying `PATH`).

```cpp
subproc::find_program_clear_cache();
```

**Note**: Setting `PATH` via `subproc::cenv["PATH"] = ...` automatically clears the cache.

## Examples

```cpp
std::string git = subproc::find_program("git");
if (!git.empty()) {
    std::println("git found at: {}", git);
}

std::string py3 = subproc::find_program("python3");
if (py3.empty()) {
    std::println("Python 3 not found");
}
```

## Related Functions

### `find_program_clear_cache()`

```cpp
void find_program_clear_cache();
```

Clears the cached program lookup results. Call this after modifying `PATH` outside of `cenv`.

## See Also

- [Environment Management](environment.md) — `cenv["PATH"]` auto-clears the cache
- [`run()`](run.md) — calls `find_program` internally to resolve `command[0]`
