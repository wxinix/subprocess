# Environment Management

[Back to README](../README.md)

Utilities for reading, modifying, and restoring environment variables and the working directory. Designed for safe, scoped environment manipulation when launching subprocesses.

## Global Environment Access — `cenv`

```cpp
inline Environ cenv;
```

`cenv` is a global object that provides dictionary-style access to the current process's environment variables.

### Reading

```cpp
std::string path = subproc::cenv["PATH"];        // returns "" if not set
if (subproc::cenv["MY_VAR"]) { /* exists */ }    // bool conversion
```

### Writing

```cpp
subproc::cenv["MY_VAR"] = "value";       // set string
subproc::cenv["COUNT"] = 42;             // set int (converted to string)
subproc::cenv["DEBUG"] = true;           // set bool → "1"
subproc::cenv["DEBUG"] = false;          // set bool → "0"
subproc::cenv["MY_VAR"] = nullptr;       // delete the variable
```

**Note**: Setting `PATH` (case-insensitive) automatically clears the `find_program` cache.

### Underlying Classes

```cpp
class Environ {
    EnvironSetter operator[](const std::string& name) const;
};

class EnvironSetter {
    operator std::string() const;        // read current value
    explicit operator bool() const;      // true if variable exists and is non-empty
    std::string to_string() const;       // read current value
    EnvironSetter& operator=(const std::string& str);
    EnvironSetter& operator=(const char* str);
    EnvironSetter& operator=(std::nullptr_t);            // delete
    EnvironSetter& operator=(T value);                   // arithmetic types
};
```

## `current_env_copy()`

```cpp
EnvMap current_env_copy();
```

Returns a snapshot of the entire current environment as a `std::map<std::string, std::string>`. Useful for creating a modified environment to pass to a subprocess.

```cpp
auto env = subproc::current_env_copy();
env["MY_VAR"] = "my_value";
env.erase("UNWANTED_VAR");

auto cp = subproc::run({"printenv"}, {.cout = PipeOption::pipe, .env = env});
```

**Important**: When you pass a non-empty `env` to `RunOptions`, it **replaces** the child's entire environment. Start from `current_env_copy()` if you want to modify rather than replace.

## `create_env_block(map)`

```cpp
std::u16string create_env_block(const EnvMap& map);
```

Converts an `EnvMap` into a Windows-format environment block (null-separated UTF-16 string pairs, double-null terminated). Used internally by `ProcessBuilder`.

## EnvGuard — Scoped Environment Snapshot

```cpp
class EnvGuard : public CwdGuard {
public:
    EnvGuard();
    ~EnvGuard();
};
```

`EnvGuard` captures a snapshot of **all** environment variables and the current working directory on construction. When the guard is destroyed, it restores the environment and working directory to their original state.

- **New variables** added during the guard's lifetime are deleted.
- **Modified variables** are restored to their original values.
- **Deleted variables** are re-created with their original values.
- **Working directory** is restored (inherited from `CwdGuard`).

```cpp
{
    subproc::EnvGuard guard;                     // snapshot
    subproc::cenv["PATH"] = "C:/custom/bin";
    subproc::cenv["TEMP_FLAG"] = "1";
    subproc::set_cwd("C:/some/dir");

    subproc::run({"my_tool"});                   // runs with modified env

}  // PATH restored, TEMP_FLAG deleted, cwd restored
```

**Note**: `EnvGuard` is not copyable. Typically used as a local variable.

## CwdGuard — Scoped Working Directory

```cpp
class CwdGuard {
public:
    CwdGuard();
    ~CwdGuard();
};
```

Captures the current working directory on construction and restores it on destruction. `EnvGuard` inherits from `CwdGuard`, so you rarely need to use `CwdGuard` directly.

```cpp
{
    subproc::CwdGuard guard;
    subproc::set_cwd("C:/temp");
    // ... work in C:/temp ...
}  // original directory restored
```

## EnvLock

```cpp
class EnvLock {
public:
    EnvLock();  // acquires a global mutex
};
```

Acquires a process-wide mutex for environment operations. Use this if you are modifying environment variables from multiple threads simultaneously.

## Helper Functions

### `getenv(name)`

```cpp
std::string getenv(const std::string& name);
```

Returns the value of an environment variable, or an empty string if not set. Thread-safe wrapper around `_dupenv_s`.

### `get_cwd()` / `set_cwd(path)`

```cpp
std::string get_cwd();
void set_cwd(const std::string& path);
```

Get or set the current working directory. Thin wrappers around `std::filesystem::current_path()`.

## See Also

- [`RunOptions`](run-options.md) — `env` and `cwd` fields
- [`run()`](run.md) — passes environment to child processes
