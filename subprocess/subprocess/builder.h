#pragma once

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "pipe.h"
#include "pipevar.hpp"

namespace subprocess
{

#ifdef _WIN32
std::string LastErrorString();
#endif

/**
 * @brief Struct representing options for configuring a subprocess.
 *
 * This struct encapsulates various options that can be used to configure
 * the behavior of a subprocess when using functions like subprocess::run().
 *
 * @note For reference:
 * - 0: stdin (cin)
 * - 1: stdout (cout)
 * - 2: stderr (cerr)
 *
 * @note Possible names for this struct include PopenOptions, RunDef,
 * RunConfig, or RunOptions.
 */
struct RunOptions
{
    /**
     * @brief Option for stdin (cin): data to pipe to stdin or a handle to use.
     *
     * If a pipe handle is used, it will be made inheritable automatically
     * when the process is created and closed on the parent's end.
     */
    PipeVar cin{PipeOption::inherit}; // NOLINT

    /**
     * @brief Option for stdout (cout): data to pipe to stdout or a handle to use.
     *
     * If a pipe handle is used, it will be made inheritable automatically
     * when the process is created and closed on the parent's end.
     */
    PipeVar cout{PipeOption::inherit}; // NOLINT

    /**
     * @brief Option for stderr (cerr): data to pipe to stderr or a handle to use.
     *
     * If a pipe handle is used, it will be made inheritable automatically
     * when the process is created and closed on the parent's end.
     */
    PipeVar cerr{PipeOption::inherit}; // NOLINT

    /**
     * @brief Set to true to run the subprocess as a new process group.
     */
    bool new_process_group{false}; // NOLINT

    /**
     * @brief Current working directory for the new process to use.
     */
    std::string cwd{}; // NOLINT

    /**
     * @brief Timeout in seconds. Raises TimeoutExpired exception if exceeded.
     *
     * This option is only available when using the subprocess_run function.
     */
    double timeout{-1}; // NOLINT

    /**
     * @brief Set to true for subprocess::run() to throw an exception if the subprocess
     * returns non-zero code. Ignored when using Popen directly.
     */
    bool raise_on_nonzero{false}; // NOLINT

    /**
     * @brief If empty, inherits environment variables from the current process.
     */
    EnvMap env{}; // NOLINT
};

class ProcessBuilder;

/**
 * @brief Represents an active running process, similar in design to
 * subprocess.Popen in Python.
 *
 * This C++ implementation provides functionality for starting and managing
 * a child process, allowing communication through input and output streams.
 *
 * @note In C++, this class aims to address shortcomings identified in the
 * subprocess.Popen design.
 */
struct Popen
{
public:
    /**
     * @brief Default constructor, initializes the object as empty and invalid.
     */
    Popen() = default;

    /**
     * @brief Constructor that starts a command with specified options.
     * @param command The command line to be executed.
     * @param options The run options for the process.
     */
    Popen(CommandLine command, const RunOptions& options);

    /**
     * @brief Constructor that starts a command with specified options.
     * @param command The command line to be executed.
     * @param options The run options for the process.
     */
    Popen(CommandLine command, RunOptions&& options);

    // Rule of Three: Deleted copy constructor and copy assignment operator
    Popen(const Popen&) = delete;
    Popen& operator=(const Popen&) = delete;

    // Move constructor and move assignment operator
    Popen(Popen&&) noexcept;
    Popen& operator=(Popen&&) noexcept;

    /**
     * @brief Destructor that waits for the process, closes pipes, and destroys
     * any handles.
     */
    ~Popen();

    /**
     * @brief Stream to send data to the process. Ownership is held by
     * this class.
     */
    PipeHandle cin{kBadPipeValue}; // NOLINT

    /**
     * @brief Stream to get output of the process. Ownership is held by
     * this class.
     */
    PipeHandle cout{kBadPipeValue}; // NOLINT

    /**
     * @brief Stream to get cerr output of the process. Ownership is held
     * by this class.
     */
    PipeHandle cerr{kBadPipeValue}; // NOLINT

    /**
     * @brief Process ID of the child process.
     */
    pid_t pid{0}; // NOLINT

    /**
     * @brief Exit value of the process. Valid once the process is completed.
     */
    int64_t returncode{kBadReturnCode}; // NOLINT

    /**
     * @brief Command line arguments used to start the process.
     */
    CommandLine args{}; // NOLINT

    /**
     * @brief Ignores and closes the cout stream.
     */
    [[maybe_unused]] void ignore_cout();

    /**
     * @brief Ignores and closes the cerr stream.
     */
    void ignore_cerr();

    /**
     * @brief Ignores and closes both cout and cerr streams if open.
     */
    [[maybe_unused]] void ignore_output();

    /**
     * @brief Checks if the process has terminated.
     * @return True if the process has terminated.
     * @throws OSError If an OS-specific error has been encountered.
     */
    [[maybe_unused]] bool poll();

    /**
     * @brief Waits for the process to finish.
     * @param timeout Timeout in seconds. Defaults to -1 (wait forever).
     * @return Return code of the process.
     * @throws OSError If there was an OS-level error.
     * @throws TimeoutExpired If the timeout has expired.
     */
    int64_t wait(double timeout = -1.0F);

    /**
     * @brief Sends a signal to the process.
     * @param signal The signal to send.
     * @return True if the signal was successfully sent.
     */
    [[nodiscard]] bool send_signal(SigNum signal) const;

    /**
     * @brief Sends SIGTERM signal; on Windows, sends CTRL_BREAK_EVENT.
     * @return True if the signal was successfully sent.
     */
    [[maybe_unused]] [[nodiscard]] bool terminate() const;

    /**
     * @brief Equivalent to send_signal(SIGKILL).
     * @return True if the signal was successfully sent.
     */
    [[maybe_unused]] [[nodiscard]] bool kill() const;

    /**
     * @brief Destructs the object and initializes it to a basic state.
     */
    void close();

    /**
     * @brief Closes the cin pipe if it is open.
     */
    void close_cin();

    friend ProcessBuilder;

private:
    /**
     * @brief Initializes the Popen object with the given command and options.
     * @param pipe The command line to be executed.
     * @param pipeOpt The run options for the process.
     */
    void init(CommandLine& pipe, RunOptions& pipeOpt);

#ifdef _WIN32
    /**
     * @brief Process information specific to Windows.
     */
    PROCESS_INFORMATION process_info{};
#endif
};

/**
 * @brief This class handles the bulk of the work for starting a process,
 * providing extensive customization options.
 *
 * This class is designed to be highly customizable, making it the most complex
 * component in the process management system.
 *
 * @note The decision to make this a public API or keep it private is
 * still undecided.
 */
class ProcessBuilder
{
public:
    /**
     * @brief List of pipe handles to be closed in the child process.
     */
    std::vector<PipeHandle> child_close_pipes{}; // NOLINT

    /**
     * @brief Pipe option for cerr (standard error) stream.
     */
    PipeOption cerr_option{PipeOption::inherit}; // NOLINT

    /**
     * @brief Pipe handle for cerr (standard error) stream.
     */
    PipeHandle cerr_pipe{kBadPipeValue}; // NOLINT

    /**
     * @brief Pipe option for cin (standard input) stream.
     */
    PipeOption cin_option{PipeOption::inherit}; // NOLINT

    /**
     * @brief Pipe handle for cin (standard input) stream.
     */
    PipeHandle cin_pipe{kBadPipeValue}; // NOLINT

    /**
     * @brief Pipe option for cout (standard output) stream.
     */
    PipeOption cout_option{PipeOption::inherit}; // NOLINT

    /**
     * @brief Pipe handle for cout (standard output) stream.
     */
    PipeHandle cout_pipe{kBadPipeValue}; // NOLINT

    /**
     * @brief Flag indicating whether to create a new process group.
     */
    bool new_process_group{false}; // NOLINT

    /**
     * @brief Command line to be executed. If empty, inherits from the
     * current process.
     */
    CommandLine command{}; // NOLINT

    /**
     * @brief Environment variables for the child process.
     */
    EnvMap env{}; // NOLINT

    /**
     * @brief Current working directory for the child process.
     */
    std::string cwd{}; // NOLINT

    /**
     * @brief Gets the Windows command string.
     * @return The Windows command string, which is supposed to be the first
     * arg of the command line.
     */
    [[maybe_unused]] std::string windows_command();

    /**
     * @brief Generates the Windows command arguments string.
     * @return The formatted Windows command arguments string.
     */
    [[maybe_unused]] [[nodiscard]] std::string windows_args() const;

    /**
     * @brief Generates the Windows command arguments string for the given
     * command line.
     *
     * @param cmd The command line for which to generate the arguments string.
     * @return The formatted Windows command arguments string.
     */
    static std::string windows_args(const CommandLine& cmd);

    /**
     * @brief Runs the process using the stored command line.
     * @return The Popen object representing the running process.
     */
    [[maybe_unused]] Popen run()
    {
        return run_command(this->command);
    }

    /**
     * @brief Runs the process with the specified command line.
     * @param cmdline The command line to be executed.
     * @return The Popen object representing the running process.
     */
    Popen run_command(const CommandLine& cmdline);
};

/**
 * @brief Executes a process to completion and provides details about the
 * execution.
 *
 * If there is data to be piped, this function will run the process to
 * completion, reading stdout/stderr if available, and storing the output
 * in cout and cerr.
 *
 * @param popen An already running process.
 * @param check If true, the function will throw a CalledProcessException
 * if the process returns a non-zero exit code.
 *
 * @return A filled out CompletedProcess structure representing the executed
 * command.
 *
 * @throws CalledProcessError If raise_on_nonzero=true and the process returned a non-zero
 * error code.
 *
 * @throws TimeoutExpired If the subprocess does not finish within the specified
 * timeout in seconds.
 *
 * @throws OSError For OS-level exceptions, such as failing OS commands.
 * @throws std::runtime_error For various runtime errors.
 * @throws std::invalid_argument If the argument is invalid for the current usage.
 * @throws std::domain_error When arguments don't make sense working together.
 */
[[maybe_unused]] CompletedProcess run(Popen& popen, bool check = false);

/**
 * @brief Runs a command, blocking until completion, and returns details about
 * the execution.
 *
 * See subprocess::run(Popen&,bool) for more details about exceptions.
 *
 * @param command The command to run. The first element must be the executable.
 * @param options Options specifying how to run the command.
 *
 * @return A CompletedProcess structure containing details about the execution.
 */
CompletedProcess run(CommandLine command, const RunOptions& options = {});

/**
 * @brief Helper class to construct RunOptions with minimal typing.
 */
struct RunBuilder
{
    RunOptions options{};  // NOLINT
    CommandLine command{}; // NOLINT;

    /**
     * @brief Default constructor.
     */
    RunBuilder() = default;

    /**
     * @brief Constructs a builder with cmd as the command to run.
     * @param cmd The command line to run.
     */
    [[maybe_unused]] RunBuilder(CommandLine cmd) : command(std::move(cmd)) //NOLINT
    {
        // Intentional not marked as explicit.
    }

    /**
     * @brief Constructs a builder with a command to run.
     * @param cmd The command line to run.
     */
    RunBuilder(std::initializer_list<std::string> cmd) : command(cmd)
    {
    }

    /**
     * @brief Only for run(), throws an exception if the command returns
     * a non-zero exit code.
     *
     * @param f Flag to enable checking.
     * @return A reference to the RunBuilder.
     */
    RunBuilder& raise_on_nonzero(bool f)
    {
        options.raise_on_nonzero = f;
        return *this;
    }

    /**
     * @brief Set the cin option. Could be PipeOption, input handle, std::string
     * with data to pass.
     *
     * @param cin The input option for the command.
     * @return A reference to the RunBuilder.
     */
    RunBuilder& cin(const PipeVar& cin)
    {
        options.cin = cin;
        return *this;
    }

    /**
     * @brief Sets the cout option. Could be a PipeOption, output handle.
     * @param cout The output option for the command.
     * @return A reference to the RunBuilder.
     */
    RunBuilder& cout(const PipeVar& cout)
    {
        options.cout = cout;
        return *this;
    }

    /**
     * @brief Sets the cerr option. Could be a PipeOption, output handle.
     * @param cerr The error output option for the command.
     * @return A reference to the RunBuilder.
     */
    RunBuilder& cerr(const PipeVar& cerr)
    {
        options.cerr = cerr;
        return *this;
    }

    /**
     * @brief Sets the current working directory to use for the subprocess.
     * @param cwd The path to the current working directory.
     * @return A reference to the RunBuilder.
     */
    [[maybe_unused]] RunBuilder& cwd(const std::string& cwd)
    {
        options.cwd = cwd;
        return *this;
    }

    /**
     * @brief Sets the environment to use. Default is the current environment
     * if unspecified.
     *
     * @param env The environment variables for the subprocess.
     * @return A reference to the RunBuilder.
     */
    [[maybe_unused]] RunBuilder& env(const EnvMap& env)
    {
        options.env = env;
        return *this;
    }

    /**
     * @brief Sets the timeout to use for run() invocation only.
     * @param timeout The timeout value in seconds.
     * @return A reference to the RunBuilder.
     */
    [[maybe_unused]] RunBuilder& timeout(double timeout)
    {
        options.timeout = timeout;
        return *this;
    }

    /**
     * @brief Sets to true to run as a new process group.
     * @param new_group Flag to indicate whether to run as a new process group.
     * @return A reference to the RunBuilder.
     */
    [[maybe_unused]] RunBuilder& new_process_group(bool new_group)
    {
        options.new_process_group = new_group;
        return *this;
    }

    /**
     * @brief Conversion operator to RunOptions.
     * @return The configured RunOptions.
     */
    operator RunOptions() const //NOLINT
    {   // Intentionally not marked explicit
        return options;
    }

    /**
     * @brief Runs the command already configured.
     *
     * See subprocess::run() for more details.
     *
     * @return A CompletedProcess containing details about the execution.
     */
    [[maybe_unused]] [[nodiscard]] CompletedProcess run() const
    {
        return subprocess::run(command, options);
    }

    /**
     * @brief Creates a Popen object running the process asynchronously.
     * @return A Popen object with the current configuration.
     */
    [[nodiscard]] Popen popen() const
    {
        return {command, options};
    }
};

/**
 * Sleeps for a number of seconds for current thread.
 * @param seconds  The number of seconds to sleep for.
 * @return How many seconds have been slept.
 */
[[maybe_unused]] double sleep_seconds(double seconds);

class StopWatch
{
public:
    StopWatch() : m_start(0.0F)
    {
        Start();
    }

    void Start()
    {
        m_start = monotonic_seconds();
    }

    [[nodiscard]] double seconds() const
    {
        return monotonic_seconds() - m_start;
    }

private:
    static double monotonic_seconds();

    double m_start;
};

#ifndef _WIN32
namespace details
{
/** @brief Throw an OS error.
 *
 * Throws an exception with the specified function name and errno code.
 *
 * @param function The name of the function where the error occurred.
 * @param ec The error number indicating the specific error.
 */
void throw_os_error(const char* function, int ec);

} // namespace details
#endif

} // namespace subprocess