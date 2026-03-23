#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <csignal>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace subprocess {
using ssize_t = intptr_t;

#ifdef _WIN32
constexpr bool kIsWin32 = true;
#else
constexpr bool kIsWin32 = false;
#endif

/** @brief Signals to send. P prefix avoids macro collisions. */
enum class SigNum {
    PSIGHUP = 1,        ///< Hangup
    PSIGINT = SIGINT,   ///< Interrupt from keyboard
    PSIGQUIT = 3,       ///< Quit from keyboard
    PSIGILL = SIGILL,   ///< Illegal Instruction
    PSIGTRAP = 5,       ///< Trace/breakpoint trap
    PSIGABRT = SIGABRT, ///< Abort signal
    PSIGIOT = 6,        ///< IOT trap (synonym for SIGABRT)
    PSIGBUS = 7,        ///< Bus error
    PSIGFPE = SIGFPE,   ///< Floating point exception
    PSIGKILL = 9,       ///< Kill signal
    PSIGUSR1 = 10,      ///< User-defined signal 1
    PSIGSEGV = SIGSEGV, ///< Invalid memory reference
    PSIGUSR2 = 12,      ///< User-defined signal 2
    PSIGPIPE = 13,      ///< Broken pipe
    PSIGALRM = 14,      ///< Timer signal
    PSIGTERM = SIGTERM, ///< Termination signal
    PSIGSTKFLT = 16,    ///< Stack fault on coprocessor
    PSIGCHLD = 17,      ///< Child stopped or terminated
    PSIGCONT = 18,      ///< Continue if stopped
    PSIGSTOP = 19,      ///< Stop process
    PSIGTSTP = 20,      ///< Stop typed at terminal
    PSIGTTIN = 21,      ///< Terminal input for background process
    PSIGTTOU = 22,      ///< Terminal output for background process
    PSIGURG = 23,       ///< Urgent condition on socket
    PSIGXCPU = 24,      ///< CPU time limit exceeded
    PSIGXFSZ = 25,      ///< File size limit exceeded
    PSIGVTALRM = 26,    ///< Virtual alarm clock
    PSIGPROF = 27,      ///< Profiling timer expired
    PSIGWINCH = 28,     ///< Window resize signal
    PSIGIO = 29         ///< I/O now possible
};

#ifndef _WIN32

using PipeHandle = int;
using pid_t = ::pid_t;

constexpr char kPathDelimiter = ':';
constexpr PipeHandle kBadPipeValue = -1;

#else

using PipeHandle = HANDLE;
using pid_t = DWORD;

constexpr char kPathDelimiter = ';';
constexpr PipeHandle kBadPipeValue = nullptr;

#endif

constexpr int kStdInValue = 0;
constexpr int kStdOutValue = 1;
constexpr int kStdErrValue = 2;
constexpr int kBadReturnCode = -1000;

/** @brief Default buffer size for pipe I/O operations (8 KB). */
constexpr size_t kPipeBufferSize = 8192;

using CommandLine = std::vector<std::string>;
using EnvMap = std::map<std::string, std::string>;

/** @brief Redirect destination for subprocess I/O. */
enum class PipeOption : int {
    inherit,  ///< Inherits the current process handle
    cout,     ///< Redirects to stdout
    cerr,     ///< Redirects to stderr
    specific, ///< Redirects to a provided pipe (made inheritable)
    pipe,     ///< Redirects to a new handle created for you
    close,    ///< Closes the pipe
    none      ///< Not connected to parent process or the console
};

struct SubprocessError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct OSError : SubprocessError {
    using SubprocessError::SubprocessError;
};

struct CommandNotFoundError : SubprocessError {
    using SubprocessError::SubprocessError;
};

struct SpawnError : OSError {
    using OSError::OSError;
};

struct TimeoutExpired : SubprocessError {
    using SubprocessError::SubprocessError;

    TimeoutExpired(const std::string& msg, CommandLine a_cmd, const double a_timeout, std::string a_cout,
                   std::string a_cerr)
        : SubprocessError{msg}, cmd{std::move(a_cmd)}, timeout{a_timeout}, cout{std::move(a_cout)},
          cerr{std::move(a_cerr)} {}

    CommandLine cmd;
    double timeout;
    std::string cout;
    std::string cerr;
};

struct CalledProcessError : SubprocessError {
    using SubprocessError::SubprocessError;

    CalledProcessError(const std::string& msg, CommandLine a_cmd, const int64_t retcode, std::string a_cout,
                       std::string a_cerr)
        : SubprocessError{msg}, returncode{retcode}, cmd{std::move(a_cmd)}, cout{std::move(a_cout)},
          cerr{std::move(a_cerr)} {}

    int64_t returncode;
    CommandLine cmd;
    std::string cout;
    std::string cerr;
};

/** @brief Details about a completed process. */
struct CompletedProcess {
    CommandLine args;
    int64_t returncode = -1;
    std::string cout;
    std::string cerr;

    explicit operator bool() const { return returncode == 0; }
};
} // namespace subprocess
