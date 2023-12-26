#pragma warning(disable : 4068)
#pragma clang diagnostic push
#pragma ide diagnostic ignored "clion-misra-cpp2008-11-0-1"
#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <csignal>
#include <format>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace subprocess
{
// ssize_t is not a standard type and not supported in MSVC
typedef intptr_t ssize_t;

#ifdef _WIN32
/** @brief True if on Windows platform.
 *
 * This constant is useful so you can use regular if statements instead of
 * ifdefs and have both branches compile, thereby reducing the chance of
 * compiler error on a different platform.
 */
constexpr bool kIsWin32 = true;
#else
constexpr bool kIsWin32 = false;
#endif

/* Windows doesn't have all of these. The numeric values are standardized.
   Posix specifies the number in the standard so most systems should be fine.
*/

/** @brief Signals to send.
 *
 * Enumerates signals with P prefix as SIGX are macros.
 * P stands for Posix as these values are as defined by Posix.
 */
enum class SigNum
{
    PSIGHUP              //
        [[maybe_unused]] //
        = 1,             ///< Hangup detected on controlling terminal or death of controlling process
    PSIGINT              //
        [[maybe_unused]] //
        = SIGINT,        ///< Interrupt from keyboard
    PSIGQUIT             //
        [[maybe_unused]] //
        = 3,             ///< Quit from keyboard
    PSIGILL              //
        [[maybe_unused]] //
        = SIGILL,        ///< Illegal Instruction
    PSIGTRAP             //
        [[maybe_unused]] //
        = 5,             ///< Trace/breakpoint trap
    PSIGABRT             //
        [[maybe_unused]] //
        = SIGABRT,       ///< Abort signal from abort(3)
    PSIGIOT              //
        [[maybe_unused]] //
        = 6,             ///< IOT trap. A synonym for SIGABRT
    PSIGBUS              //
        [[maybe_unused]] //
        = 7,             ///< Bus error (bad memory access)
    PSIGFPE              //
        [[maybe_unused]] //
        = SIGFPE,        ///< Floating point exception
    PSIGKILL             //
        [[maybe_unused]] //
        = 9,             ///< Kill signal
    PSIGUSR1             //
        [[maybe_unused]] //
        = 10,            ///< User-defined signal 1
    PSIGSEGV             //
        [[maybe_unused]] //
        = SIGSEGV,       ///< Invalid memory reference
    PSIGUSR2             //
        [[maybe_unused]] //
        = 12,            ///< User-defined signal 2
    PSIGPIPE             //
        [[maybe_unused]] //
        = 13,            ///< Broken pipe: write to pipe with no readers
    PSIGALRM             //
        [[maybe_unused]] //
        = 14,            ///< Timer signal from alarm(2)
    PSIGTERM             //
        [[maybe_unused]] //
        = SIGTERM,       ///< Termination signal
    PSIGSTKFLT           //
        [[maybe_unused]] //
        = 16,            ///< Stack fault on coprocessor (unused)
    PSIGCHLD             //
        [[maybe_unused]] //
        = 17,            ///< Child stopped or terminated
    PSIGCONT             //
        [[maybe_unused]] //
        = 18,            //
    PSIGSTOP             ///< Stop process
        [[maybe_unused]] //
        = 19,            ///< Continue if stopped
    PSIGTSTP             //
        [[maybe_unused]] //
        = 20,            ///< Stop typed at terminal
    PSIGTTIN             //
        [[maybe_unused]] //
        = 21,            ///< Terminal input for background process
    PSIGTTOU             //
        [[maybe_unused]] //
        = 22,            ///< Terminal output for background process
    PSIGURG              //
        [[maybe_unused]] //
        = 23,            ///< Urgent condition on socket (4.2BSD)
    PSIGXCPU             //
        [[maybe_unused]] //
        = 24,            ///< CPU time limit exceeded (4.2BSD)
    PSIGXFSZ             //
        [[maybe_unused]] //
        = 25,            ///< File size limit exceeded (4.2BSD)
    PSIGVTALRM           //
        [[maybe_unused]] //
        = 26,            ///< Virtual alarm clock (4.2BSD)
    PSIGPROF             //
        [[maybe_unused]] //
        = 27,            ///< Profiling timer expired
    PSIGWINCH            //
        [[maybe_unused]] //
        = 28,            ///< Window resize signal (4.3BSD, Sun)
    PSIGIO               //
        [[maybe_unused]] //
        = 29             ///< I/O now possible (4.2BSD)
};

#ifndef _WIN32

typedef int PipeHandle;
typedef ::pid_t pid_t;

/** @brief The path separator for the PATH environment variable. */
constexpr char kPathDelimiter = ':';

/** @brief The value representing an invalid pipe. */
const PipeHandle kBadPipeValue = (PipeHandle)-1;

#else

typedef HANDLE PipeHandle;
typedef DWORD pid_t;

constexpr char kPathDelimiter = ';';
constexpr PipeHandle kBadPipeValue = nullptr;

#endif

constexpr int kStdInValue = 0;
constexpr int kStdOutValue = 1;
constexpr int kStdErrValue = 2;

/** @brief The value representing an invalid exit code possible for a process. */
constexpr int kBadReturnCode = -1000;

typedef std::vector<std::string> CommandLine;
typedef std::map<std::string, std::string> EnvMap;

/** @brief Redirect destination.
 *
 * Enumerates the redirect destinations for subprocess output.
 */
enum class PipeOption : int
{
    inherit,  ///< Inherits the current process handle
    cout,     ///< Redirects to stdout
    cerr,     ///< Redirects to stderr
    specific, ///< Redirects to a provided pipe (made inheritable)
    pipe,     ///< Redirects to a new handle created for you
    close,    ///< Closes the pipe (troll the child)
    none      ///< No file descriptor, i.e., not connected to parent process or the console.
};

/*
 * When you use using Base::Base; in a derived class, it means that the
 * constructors from the base class become public in the derived class.
 * It is a form of constructor inheritance, making the base class constructors
 * available in the derived class with the same access level they had in
 * the base class.
 */
struct SubprocessError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct OSError : SubprocessError
{
    using SubprocessError::SubprocessError;
};

struct CommandNotFoundError : SubprocessError
{
    using SubprocessError::SubprocessError;
};

struct SpawnError : OSError
{
    using OSError::OSError;
};

struct TimeoutExpired : SubprocessError
{
    using SubprocessError::SubprocessError;
    TimeoutExpired(const std::string& msg, CommandLine a_cmd, double a_timeout, std::string a_cout, std::string a_cerr)
        : SubprocessError{msg}, cmd{std::move(a_cmd)}, timeout{a_timeout}, cout{std::move(a_cout)},
          cerr{std::move(a_cerr)}
    {
    }

    CommandLine cmd;  ///< The command that was running
    double timeout;   ///< The specified timeout
    std::string cout; ///< Captured stdout
    std::string cerr; ///< Captured stderr
};

struct CalledProcessError : SubprocessError
{
    using SubprocessError::SubprocessError;

    CalledProcessError(const std::string& msg, CommandLine a_cmd, int64_t retcode, std::string a_cout, std::string a_cerr)
        : SubprocessError{msg}, cmd{std::move(a_cmd)}, returncode{retcode}, cout{std::move(a_cout)},
          cerr{std::move(a_cerr)}
    {
    }

    int64_t returncode; ///< Exit status of the child process
    CommandLine cmd;    ///< Command used to spawn the child process
    std::string cout;   ///< Stdout output if it was captured
    std::string cerr;   ///< Stderr output if it was captured
};

/** @brief Details about a completed process. */
struct CompletedProcess
{
    CommandLine args;        ///< Args used for the process (including executable)
    int64_t returncode = -1; ///< Negative number -N means terminated by signal N
    std::string cout;        ///< Captured stdout
    std::string cerr;        ///< Captured stderr

    /** @brief Implicit conversion to bool.
     *
     * Allows using CompletedProcess in a boolean context.
     */
    explicit operator bool() const
    {
        return returncode == 0;
    }
};

} // namespace subprocess
#pragma clang diagnostic pop