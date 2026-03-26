# Signal Handling

[Back to README](../README.md)

Windows signal handling in subproc, including the `SigNum` enum and how signals map to Windows process control mechanisms.

## SigNum Enum

```cpp
enum class SigNum {
    PSIGHUP = 1,     PSIGINT = 2,     PSIGQUIT = 3,
    PSIGILL = 4,     PSIGTRAP = 5,    PSIGABRT = 6,
    PSIGIOT = 6,     PSIGBUS = 7,     PSIGFPE = 8,
    PSIGKILL = 9,    PSIGUSR1 = 10,   PSIGSEGV = 11,
    PSIGUSR2 = 12,   PSIGPIPE = 13,   PSIGALRM = 14,
    PSIGTERM = 15,   PSIGSTKFLT = 16, PSIGCHLD = 17,
    PSIGCONT = 18,   PSIGSTOP = 19,   PSIGTSTP = 20,
    PSIGTTIN = 21,   PSIGTTOU = 22,   PSIGURG = 23,
    PSIGXCPU = 24,   PSIGXFSZ = 25,   PSIGVTALRM = 26,
    PSIGPROF = 27,   PSIGWINCH = 28,  PSIGIO = 29
};
```

POSIX-style signal numbers prefixed with `P` to avoid conflicts with Windows macros. On Windows, only three have distinct behavior — all others map to `CTRL_BREAK_EVENT`.

## Windows Signal Mapping

| SigNum | Windows Action | Description |
|--------|----------------|-------------|
| `PSIGKILL` (9) | `TerminateProcess` | Immediate, forceful termination. Cannot be caught. Also terminates child processes. |
| `PSIGINT` (2) | `GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)` | Sends Ctrl+C to the **entire console process group** (group ID `0`). |
| All others | `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)` | Sends Ctrl+Break to the process group identified by `pid`. |

## Popen Signal Methods

| Method | Equivalent | Description |
|--------|------------|-------------|
| `terminate()` | `send_signal(PSIGTERM)` | Sends `CTRL_BREAK_EVENT`. Graceful shutdown. |
| `kill()` | `send_signal(PSIGKILL)` | Calls `TerminateProcess`. Hard kill. |
| `kill()` with soft_kill | `send_signal(PSIGTERM)` | Sends `CTRL_BREAK_EVENT` instead of `TerminateProcess`. |

## Important: Process Groups

`CTRL_BREAK_EVENT` requires `new_process_group = true` to target only the child process. Without it, the signal may affect the parent or other processes sharing the console.

```cpp
auto proc = subproc::RunBuilder({"server"})
    .new_process_group(true)    // required for targeted signals
    .popen();

proc.terminate();  // sends CTRL_BREAK_EVENT to this process only
proc.wait();
proc.close();
```

## CTRL+C Caveat

`PSIGINT` sends `CTRL_C_EVENT` with group ID `0`, which affects **all** processes attached to the current console — including the parent. This is a Windows limitation. See [Microsoft docs on GenerateConsoleCtrlEvent](https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent).

For targeted graceful shutdown, prefer `terminate()` (which uses `CTRL_BREAK_EVENT`) with `new_process_group = true`.

## Soft Kill Mode

By default, `kill()` calls `TerminateProcess`, which immediately ends the process without cleanup. Enable soft kill to make `kill()` behave like `terminate()`:

```cpp
proc.enable_soft_kill(true);
proc.kill();  // now sends CTRL_BREAK_EVENT instead of TerminateProcess
```

This is useful when you want a uniform `kill()` call but still allow the child to perform cleanup.

## Deviations from Python

| Behavior | Python `subprocess` | subproc |
|----------|---------------------|---------|
| `terminate()` | Calls `TerminateProcess` | Sends `CTRL_BREAK_EVENT` (graceful) |
| `kill()` | Calls `TerminateProcess` | Calls `TerminateProcess` (or `CTRL_BREAK` with soft kill) |

In Python on Windows, both `terminate()` and `kill()` call `TerminateProcess`. In subproc, `terminate()` sends a graceful signal, giving the child a chance to clean up.

## See Also

- [`Popen`](popen.md) — `send_signal()`, `terminate()`, `kill()`
- [`RunOptions`](run-options.md) — `new_process_group` flag
