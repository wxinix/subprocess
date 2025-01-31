#include "builder.h"

#ifndef _WIN32
#include <spawn.h>
#ifdef __APPLE__
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include <errno.h>
#include <signal.h>
#else
#include "tlhelp32.h"
#endif

#include <mutex>
#include <thread>

#include "shellutils.h"
#include "utf8_to_utf16.h"

namespace subprocess
{

using std::nullptr_t;

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
void throw_os_error(const char* function, int ec)
{
    if (ec != 0)
    {
        char buf[256];
        // Returns zero if the entire message was successfully stored in buf,
        // non-zero otherwise. No more than bufsz-1 bytes are written, the buffer
        // is always null-terminated. If the message had to be truncated to fit
        // the buffer and bufsz is greater than 3, then only bufsz-4 bytes are
        // written, and the characters "..." are appended before the null
        // terminator
        (void)strerror_s(&buf[0], sizeof(buf), ec);
        std::string message = std::format("{} failed with code {}:{}", std::string{function}, std::to_string(ec), buf);
        throw OSError(message);
    }
}

} // namespace details
#endif

[[maybe_unused]] double sleep_seconds(const double seconds)
{
    StopWatch watch;
    std::chrono::duration<double> duration(seconds);
    std::this_thread::sleep_for(duration);
    return watch.seconds();
}

struct AutoClosePipe
{
    AutoClosePipe(PipeHandle handle, bool autoclose)
    {
        m_handle = autoclose ? handle : kBadPipeValue;
    }

    ~AutoClosePipe()
    {
        close();
    }

private:
    void close()
    {
        if (m_handle != kBadPipeValue)
        {
            (void)pipe_close(m_handle);
            m_handle = kBadPipeValue;
        }
    }

    PipeHandle m_handle;
};

void pipe_thread(PipeHandle input, std::ostream* output)
{
    std::thread thread(
        [=]()
        {
            std::vector<char> buffer(2048U);
            while (true)
            {
                ssize_t transfered = pipe_read(input, buffer.data(), buffer.size());
                if (transfered > 0)
                {
                    (void)output->write(buffer.data(), transfered);
                }
                else
                {
                    break;
                }
            }
        });
    thread.detach();
}

void pipe_thread(PipeHandle input, FILE* output)
{
    std::thread thread(
        [=]()
        {
            std::vector<char> buffer(2048U);
            while (true)
            {
                ssize_t transfered = pipe_read(input, buffer.data(), buffer.size());
                if (transfered > 0)
                {
                    (void)fwrite(buffer.data(), 1U, static_cast<size_t>(transfered), output);
                }
                else
                {
                    break;
                }
            }
        });
    thread.detach();
}

void pipe_thread(FILE* input, PipeHandle output, bool bautoclose)
{
    std::thread thread(
        [=]()
        {
            AutoClosePipe autoclose(output, bautoclose);
            std::vector<char> buffer(2048U);
            while (true)
            {
                size_t transfered = fread(buffer.data(), 1U, buffer.size(), input);
                if (transfered > 0U)
                {
                    (void)pipe_write(output, buffer.data(), transfered);
                }
                else
                {
                    break;
                }
            }
        });
    thread.detach();
}

void pipe_thread(const std::string& input, [[maybe_unused]] PipeHandle output, [[maybe_unused]] bool bautoclose)
{
    std::thread thread(
        [input, output, bautoclose]()
        {
            AutoClosePipe autoclose(output, bautoclose);

            std::size_t pos = 0U;
            while (pos < input.size())
            {
                ssize_t transfered = pipe_write(output, input.c_str() + pos, input.size() - pos);
                if (transfered > 0)
                {
                    pos += static_cast<size_t>(transfered);
                }
                else
                {
                    break;
                }
            }
        });
    thread.detach();
}

void pipe_thread(std::istream* input, PipeHandle output, bool bautoclose)
{
    std::thread thread(
        [=]()
        {
            AutoClosePipe autoclose(output, bautoclose);
            std::vector<char> buffer(2048U);
            while (true)
            {
                (void)input->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                ssize_t transfered = input->gcount();
                if (input->bad() || (transfered <= 0 && input->eof()))
                {
                    break;
                }

                if (transfered > 0)
                {
                    (void)pipe_write(output, &buffer[0U], static_cast<size_t>(transfered));
                }
            }
        });
    thread.detach();
}

void setup_redirect_stream(PipeHandle input, PipeVar& output)
{
    auto index = static_cast<PipeVarIndex>(output.index());

    if (index == PipeVarIndex::istream)
    {
        throw std::domain_error("expected something to output to");
    }
    else
    {
        switch (index)
        {
            case PipeVarIndex::ostream:
                pipe_thread(input, std::get<std::ostream*>(output));
                break;

            case PipeVarIndex::file:
                pipe_thread(input, std::get<FILE*>(output));
                break;

            default:
                //  PipeVarIndex::handle, PipeVarIndex::option, PipeVarIndex::string
                break;
        }
    }
}

bool setup_redirect_stream(PipeVar& input, PipeHandle output)
{
    auto index = static_cast<PipeVarIndex>(input.index());
    bool result;

    if (index == PipeVarIndex::ostream)
    {
        throw std::domain_error("reading from std::ostream doesn't make sense");
    }
    else
    {
        switch (index)
        {
            case PipeVarIndex::string:
            {
                pipe_thread(std::get<std::string>(input), output, true);
                result = true;
                break;
            }

            case PipeVarIndex::istream:
            {
                pipe_thread(std::get<std::istream*>(input), output, true);
                result = true;
                break;
            }

            case PipeVarIndex::file:
            {
                pipe_thread(std::get<FILE*>(input), output, true);
                result = true;
                break;
            }

            default:
            {
                // PipeVarIndex::handle, PipeVarIndex::option
                result = false;
                break;
            }
        }
    }

    return result;
}

Popen::Popen(CommandLine command, const RunOptions& optionsIn)
{
    // We have to make a copy because of const
    RunOptions options = optionsIn;
    init(command, options);
}

Popen::Popen(CommandLine command, RunOptions&& optionsIn)
{
    RunOptions options = std::move(optionsIn);
    init(command, options);
}

void Popen::init(CommandLine& command, RunOptions& options)
{
    ProcessBuilder builder;

    auto setPipeOption = [](PipeHandle& pipe, PipeOption& pipeOpt, PipeVar& pipeVar, const std::string& errMsg)
    {
        if (pipeOpt = get_pipe_option(pipeVar); pipeOpt == PipeOption::specific)
        {
            if (pipe = std::get<PipeHandle>(pipeVar); pipe == kBadPipeValue)
            {
                throw std::invalid_argument(errMsg);
            }
        }
    };

    setPipeOption(builder.cin_pipe,   //
                  builder.cin_option, //
                  options.cin,        //
                  "Bad pipe value for cin");

    setPipeOption(builder.cout_pipe,   //
                  builder.cout_option, //
                  options.cout,        //
                  "Bad pipe value for cout");

    setPipeOption(builder.cerr_pipe,   //
                  builder.cerr_option, //
                  options.cerr,        //
                  "Bad pipe value for cerr");

    builder.new_process_group = options.new_process_group;
    builder.create_no_window = options.create_no_window;
    builder.detached_process = options.detached_process;
    builder.env = options.env;
    builder.cwd = options.cwd;

    *this = builder.run_command(command);

    if (setup_redirect_stream(options.cin, cin))
    {
        cin = kBadPipeValue;
    }
    setup_redirect_stream(cout, options.cout);
    setup_redirect_stream(cerr, options.cerr);
}

Popen::Popen(Popen&& other) noexcept
{
    *this = std::move(other);
}

Popen& Popen::operator=(Popen&& other) noexcept
{
    close();
    cin = other.cin;
    cout = other.cout;
    cerr = other.cerr;

    pid = other.pid;
    returncode = other.returncode;
    args = std::move(other.args);
    m_soft_kill = other.m_soft_kill;

#ifdef _WIN32
    process_info = other.process_info;
    other.process_info = {};
#endif

    other.cin = kBadPipeValue;
    other.cout = kBadPipeValue;
    other.cerr = kBadPipeValue;
    other.pid = 0U;
    other.returncode = kBadReturnCode;
    return *this;
}

Popen::~Popen()
{
    close();
}

void Popen::close()
{
    if (cin != kBadPipeValue)
    {
        (void)pipe_close(cin);
        cin = kBadPipeValue;
    }

    if (cout != kBadPipeValue)
    {
        (void)pipe_close(cout);
        cout = kBadPipeValue;
    }

    if (cerr != kBadPipeValue)
    {
        (void)pipe_close(cerr);
        cerr = kBadPipeValue;
    }

    // do this to not have zombie processes.
    if (pid > 0U)
    {
        (void)wait();
#ifdef _WIN32
        (void)CloseHandle(process_info.hProcess);
        (void)CloseHandle(process_info.hThread);
#endif
    }

    pid = 0U;
    returncode = kBadReturnCode;
    args.clear();
}

#ifdef _WIN32
std::string LastErrorString()
{
    LPTSTR lpMsgBuf = nullptr;
    DWORD dw = GetLastError();

    FormatMessage(
        static_cast<DWORD>(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS),
        nullptr,                             //
        dw,                                  //
        0U,                                  //
        reinterpret_cast<LPTSTR>(&lpMsgBuf), //
        0U,                                  //
        nullptr);

    std::string message = lptstr_to_string(static_cast<LPTSTR>(lpMsgBuf));
    (void)LocalFree(lpMsgBuf);
    return message;
}

[[maybe_unused]] bool Popen::poll()
{
    bool result{false};

    if (this->returncode != kBadReturnCode)
    {
        result = true;
    }
    else
    {
        DWORD ms = 0U;
        DWORD wr = WaitForSingleObject(process_info.hProcess, ms);
        if (wr == WAIT_TIMEOUT) // NOLINT
        {
            result = false;
        }
        else if (wr == WAIT_ABANDONED) // NOLINT
        {
            DWORD error = GetLastError();
            throw OSError("WAIT_ABANDONED error:" + std::to_string(error));
        }
        else if (wr == WAIT_FAILED) // NOLINT
        {
            DWORD error = GetLastError();
            throw OSError("WAIT_FAILED error:" + std::to_string(error) + ":" + LastErrorString());
        }
        else if (wr != WAIT_OBJECT_0) // NOLINT
        {
            throw OSError("WaitForSingleObject failed: " + std::to_string(wr));
        }
        else
        {
            DWORD excode;
            int ret = GetExitCodeProcess(process_info.hProcess, &excode);
            if (ret == 0)
            {
                DWORD error = GetLastError();
                throw OSError("GetExitCodeProcess failed: " + std::to_string(error) + ":" + LastErrorString());
            }
            returncode = static_cast<int64_t>(excode);
            result = true;
        }
    }
    return result;
}

int64_t Popen::wait(double timeout)
{
    if (this->returncode == kBadReturnCode)
    {
        auto ms = static_cast<DWORD>(timeout < 0.0F ? INFINITE : timeout * 1000.0);
        DWORD wr = WaitForSingleObject(process_info.hProcess, ms);
        if (wr == WAIT_TIMEOUT) // NOLINT
        {
            throw TimeoutExpired("timeout of " + std::to_string(ms) + " expired");
        }
        else if (wr == WAIT_ABANDONED) // NOLINT
        {
            DWORD error = GetLastError();
            throw OSError("WAIT_ABANDONED error:" + std::to_string(error));
        }
        else if (wr == WAIT_FAILED) // NOLINT
        {
            DWORD error = GetLastError();
            throw OSError("WAIT_FAILED error:" + std::to_string(error) + ":" + LastErrorString());
        }
        else if (wr != WAIT_OBJECT_0) // NOLINT
        {
            throw OSError("WaitForSingleObject failed: " + std::to_string(wr));
        }
        else
        {
            DWORD excode;
            int ret = GetExitCodeProcess(process_info.hProcess, &excode);
            if (ret == 0)
            {
                DWORD error = GetLastError();
                throw OSError("GetExitCodeProcess failed: " + std::to_string(error) + ":" + LastErrorString());
            }
            returncode = static_cast<int64_t>(excode); // Widening conversion, fine.
        }
    }

    return this->returncode;
}

[[maybe_unused]] void TerminateChildProcesses(DWORD parentProcessID)
{
    // Take a snapshot of all processes in the system
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        return;
    }

    // Initialize the process entry structure
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    // Retrieve information about the first process in the snapshot
    if (Process32First(hSnapshot, &pe32))
    {
        do
        {
            // Check if the process is a child of the specified parent
            if (pe32.th32ParentProcessID == parentProcessID)
            {
                // Open the child process to obtain a handle
                HANDLE hChildProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                if (hChildProcess != nullptr)
                {
                    // Terminate the child process
                    TerminateProcess(hChildProcess, 0);
                    // Close the handle to the child process
                    CloseHandle(hChildProcess);
                }
            }
        } while (Process32Next(hSnapshot, &pe32));
    }

    // Close the process snapshot handle
    CloseHandle(hSnapshot);
}

std::vector<DWORD> GetChildProcessIDs(DWORD parentProcessID)
{
    std::vector<DWORD> childProcessIDs;

    // Take a snapshot of all processes in the system
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        return childProcessIDs;
    }

    // Initialize the process entry structure
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    // Retrieve information about the first process in the snapshot
    if (Process32First(hSnapshot, &pe32))
    {
        do
        {
            // Check if the process is a child of the specified parent
            if (pe32.th32ParentProcessID == parentProcessID)
            {
                // Add the child process ID to the vector
                childProcessIDs.push_back(pe32.th32ProcessID);
            }
        } while (Process32Next(hSnapshot, &pe32));
    }

    // Close the process snapshot handle
    CloseHandle(hSnapshot);

    return childProcessIDs;
}

void TerminateProcessByID(DWORD processID)
{
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processID);
    if (hProcess != nullptr)
    {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
    }
}

bool Popen::send_signal(SigNum signum) const
{
    bool result;

    if (returncode != kBadReturnCode)
    {
        result = false;
    }
    else
    {
        if (signum == SigNum::PSIGKILL)
        {
            auto ids = GetChildProcessIDs(process_info.dwProcessId);
            // 137 just like when a process is killed SIGKILL
            result = TerminateProcess(process_info.hProcess, 137U);
            for (auto id: ids)
            {
                TerminateProcessByID(id);
            }
        }
        else if (signum == SigNum::PSIGINT)
        {
            // pid can be used as a process group id.The signals are sent to the
            // entire process group, including parents.This event simulates the
            // pressing of Ctrl+C in the console window, the associated process
            // may handle the event to perform cleanup or terminate gracefully.
            result = GenerateConsoleCtrlEvent(static_cast<DWORD>(CTRL_C_EVENT), 0); // pid must be 0
        }
        else
        { // Simulates the pressing of Ctrl+Break, triggering a more forceful
            // termination of the process.
            result = GenerateConsoleCtrlEvent(static_cast<DWORD>(CTRL_BREAK_EVENT), pid);
        }
    }

    if (!result)
    {
        std::string str = LastErrorString();
        std::cout << "error: " << str << "\n";
    }

    return result;
}
#else

#endif

[[maybe_unused]] bool Popen::terminate() const
{
    return send_signal(SigNum::PSIGTERM);
}

[[maybe_unused]] bool Popen::kill() const
{
    return m_soft_kill ? send_signal(SigNum::PSIGTERM) : send_signal(SigNum::PSIGKILL);
}

[[maybe_unused]] void Popen::ignore_cout()
{
    pipe_ignore_and_close(cout);
    cout = kBadPipeValue;
}

void Popen::ignore_cerr()
{
    pipe_ignore_and_close(cerr);
    cerr = kBadPipeValue;
}

[[maybe_unused]] void Popen::ignore_output()
{
    ignore_cout();
    ignore_cerr();
}

void Popen::close_cin()
{
    if (cin != kBadPipeValue)
    {
        (void)pipe_close(cin);
        cin = kBadPipeValue;
    }
}

[[maybe_unused]] std::string ProcessBuilder::windows_command()
{
    return this->command[0U];
}

[[maybe_unused]] std::string ProcessBuilder::windows_args() const
{
    return ProcessBuilder::windows_args(this->command);
}

std::string ProcessBuilder::windows_args(const CommandLine& cmd)
{
    std::string args;
    for (unsigned int i = 0U; i < cmd.size(); ++i)
    {
        if (i > 0U)
        {
            args += ' ';
        }

#ifdef _WIN32
        args += escape_shell_arg(cmd[i], false); // Do not add quote
#else
        args += escape_shell_arg(cmd[i]);
#endif
    }
    return args;
}

[[maybe_unused]] CompletedProcess run(Popen& popen, bool check)
{
    CompletedProcess completed;
    std::thread cout_thread;
    std::thread cerr_thread;
    if (popen.cout != kBadPipeValue)
    {
        cout_thread = std::thread(
            [&]()
            {
                try
                {
                    completed.cout = pipe_read_all(popen.cout);
                }
                catch (...)
                {
                }
                (void)pipe_close(popen.cout);
                popen.cout = kBadPipeValue;
            });
    }
    if (popen.cerr != kBadPipeValue)
    {
        cerr_thread = std::thread(
            [&]()
            {
                try
                {
                    completed.cerr = pipe_read_all(popen.cerr);
                }
                catch (...)
                {
                }
                (void)pipe_close(popen.cerr);
                popen.cerr = kBadPipeValue;
            });
    }

    if (cout_thread.joinable())
    {
        cout_thread.join();
    }

    if (cerr_thread.joinable())
    {
        cerr_thread.join();
    }

    (void)popen.wait();
    completed.returncode = popen.returncode;
    completed.args = CommandLine(popen.args.begin() + 1, popen.args.end());
    if (check)
    {
        throw CalledProcessError{"failed to execute " + popen.args[0U], popen.args, completed.returncode,
                                 completed.cout, completed.cerr};
    }
    return completed;
}

CompletedProcess run(CommandLine command, const RunOptions& options)
{
    Popen popen(command, options);
    CompletedProcess completed;
    std::thread cout_thread;
    std::thread cerr_thread;

    if (popen.cout != kBadPipeValue)
    {
        cout_thread = std::thread(
            [&]()
            {
                try
                {
                    completed.cout = pipe_read_all(popen.cout);
                }
                catch (...)
                {
                }
                (void)pipe_close(popen.cout);
                popen.cout = kBadPipeValue;
            });
    }

    if (popen.cerr != kBadPipeValue)
    {
        cerr_thread = std::thread(
            [&]()
            {
                try
                {
                    completed.cerr = pipe_read_all(popen.cerr);
                }
                catch (...)
                {
                }
                (void)pipe_close(popen.cerr);
                popen.cerr = kBadPipeValue;
            });
    }

    if (cout_thread.joinable())
    {
        cout_thread.join();
    }

    if (cerr_thread.joinable())
    {
        cerr_thread.join();
    }

    try
    {
        (void)popen.wait(options.timeout);
    }
    catch (subprocess::TimeoutExpired&)
    {
        (void)popen.send_signal(subprocess::SigNum::PSIGTERM);
        (void)popen.wait();
        throw subprocess::TimeoutExpired{"subprocess::run timeout reached", command, options.timeout, completed.cout,
                                         completed.cerr};
    }

    completed.returncode = popen.returncode;
    completed.args = command;
    if (options.raise_on_nonzero && completed.returncode != 0)
    {
        throw CalledProcessError{"failed to execute " + command[0U], command, completed.returncode, completed.cout,
                                 completed.cerr};
    }
    return completed;
}

double StopWatch::monotonic_seconds()
{
    static bool needs_init = true;
    static std::chrono::steady_clock::time_point begin;
    static double last_value = 0.0F;

    if (needs_init)
    {
        begin = std::chrono::steady_clock::now();
        needs_init = false;
    }

    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    std::chrono::duration<double> duration = now - begin;
    double result = duration.count();

    // Some OS's have bugs and not exactly monotonic, or perhaps there are
    // floating point errors or something. I don't know.
    if (result < last_value)
    {
        result = last_value;
    }
    else
    {
        last_value = result;
    }
    return result;
}

} // namespace subprocess