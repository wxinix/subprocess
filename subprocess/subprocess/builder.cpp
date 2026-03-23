#include "builder.h"

#ifndef _WIN32
#include <spawn.h>
#ifdef __APPLE__
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include <cerrno>
#include <csignal>
#else
#include "tlhelp32.h"
#endif

#include <array>
#include <format>
#include <iostream>
#include <thread>

#include "shellutils.h"
#include "utf8_to_utf16.h"

namespace subprocess {
#ifndef _WIN32
namespace details {
void throw_os_error(const char* function, const int ec) {
    if (ec != 0) {
        char buf[256];
        (void)strerror_s(&buf[0], sizeof(buf), ec);
        throw OSError(std::format("{} failed with code {}:{}", function, ec, buf));
    }
}
} // namespace details
#endif

double sleep_seconds(const double seconds) {
    const StopWatch watch;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    return watch.seconds();
}

// RAII pipe closer for thread safety
struct AutoClosePipe {
    AutoClosePipe(const PipeHandle handle, const bool autoclose) : m_handle(autoclose ? handle : kBadPipeValue) {}

    ~AutoClosePipe() {
        if (m_handle != kBadPipeValue) (void)pipe_close(m_handle);
    }

    AutoClosePipe(const AutoClosePipe&) = delete;
    AutoClosePipe& operator=(const AutoClosePipe&) = delete;

private:
    const PipeHandle m_handle;
};

// Templated pipe-reader thread: eliminates duplicate overloads for ostream*/FILE*
template<typename OutputSink>
    requires(std::same_as<OutputSink, std::ostream*> || std::same_as<OutputSink, FILE*>)
void pipe_reader_thread(const PipeHandle input, const OutputSink output) {
    std::thread([=]() {
        std::array<char, kPipeBufferSize> buffer{};
        for (ssize_t n; (n = pipe_read(input, buffer.data(), buffer.size())) > 0;) {
            if constexpr (std::same_as<OutputSink, std::ostream*>)
                (void)output->write(buffer.data(), n);
            else
                (void)fwrite(buffer.data(), 1, static_cast<size_t>(n), output);
        }
    }).detach();
}

void pipe_thread(FILE* input, const PipeHandle output, const bool autoclose) {
    std::thread([=]() {
        const AutoClosePipe guard(output, autoclose);
        std::array<char, kPipeBufferSize> buffer{};
        for (size_t n; (n = fread(buffer.data(), 1, buffer.size(), input)) > 0;)
            (void)pipe_write(output, buffer.data(), n);
    }).detach();
}

void pipe_thread(const std::string& input, const PipeHandle output, const bool autoclose) {
    std::thread([input, output, autoclose]() {
        const AutoClosePipe guard(output, autoclose);
        size_t pos = 0;
        while (pos < input.size()) {
            const auto n = pipe_write(output, input.c_str() + pos, input.size() - pos);
            if (n <= 0) break;
            pos += static_cast<size_t>(n);
        }
    }).detach();
}

void pipe_thread(std::istream* input, const PipeHandle output, const bool autoclose) {
    std::thread([=]() {
        const AutoClosePipe guard(output, autoclose);
        std::array<char, kPipeBufferSize> buffer{};
        while (true) {
            (void)input->read(buffer.data(), buffer.size());
            const auto n = input->gcount();
            if (input->bad() || (n <= 0 && input->eof())) break;
            if (n > 0) (void)pipe_write(output, buffer.data(), static_cast<size_t>(n));
        }
    }).detach();
}

// std::visit with overloaded for type-safe variant dispatch
void setup_redirect_stream(const PipeHandle input, PipeVar& output) {
    std::visit(overloaded{
                   [](std::istream*) { throw std::domain_error("expected something to output to"); },
                   [input](std::ostream* os) { pipe_reader_thread(input, os); },
                   [input](FILE* f) { pipe_reader_thread(input, f); },
                   [](const auto&) {}, // PipeOption, string, PipeHandle — no action
               },
               output);
}

bool setup_redirect_stream(PipeVar& input, const PipeHandle output) {
    return std::visit(
        overloaded{
            [](PipeOption) { return false; },
            [](PipeHandle) { return false; },
            [](std::ostream*) -> bool { throw std::domain_error("reading from std::ostream doesn't make sense"); },
            // Generic lambda: handles string, istream*, FILE* — all call pipe_thread(source, output, true)
            [output](auto&& source) {
                pipe_thread(source, output, true);
                return true;
            },
        },
        input);
}

Popen::Popen(const CommandLine& command, RunOptions options) {
    init(command, options);
}

void Popen::init(const CommandLine& command, RunOptions& options) {
    ProcessBuilder builder;

    // Lambda to configure pipe option + handle for cin/cout/cerr
    const auto setPipeOption = [](PipeHandle& pipe, PipeOption& pipeOpt, const PipeVar& pipeVar, const char* errMsg) {
        pipeOpt = get_pipe_option(pipeVar);
        if (pipeOpt == PipeOption::specific) {
            pipe = std::get<PipeHandle>(pipeVar);
            if (pipe == kBadPipeValue) throw std::invalid_argument(errMsg);
        }
    };

    setPipeOption(builder.cin_pipe, builder.cin_option, options.cin, "Bad pipe value for cin");
    setPipeOption(builder.cout_pipe, builder.cout_option, options.cout, "Bad pipe value for cout");
    setPipeOption(builder.cerr_pipe, builder.cerr_option, options.cerr, "Bad pipe value for cerr");

    builder.new_process_group = options.new_process_group;
    builder.create_no_window = options.create_no_window;
    builder.detached_process = options.detached_process;
    builder.env = std::move(options.env);
    builder.cwd = std::move(options.cwd);

    *this = builder.run_command(command);

    if (setup_redirect_stream(options.cin, this->cin)) this->cin = kBadPipeValue;

    setup_redirect_stream(this->cout, options.cout);
    setup_redirect_stream(this->cerr, options.cerr);
}

Popen::Popen(Popen&& other) noexcept {
    *this = std::move(other);
}

Popen& Popen::operator=(Popen&& other) noexcept {
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
    other.pid = 0;
    other.returncode = kBadReturnCode;
    return *this;
}

Popen::~Popen() {
    close();
}

// Helper lambda to close-and-reset a pipe handle, used in close()
void Popen::close() {
    const auto close_pipe = [](PipeHandle& h) {
        if (h != kBadPipeValue) {
            (void)pipe_close(h);
            h = kBadPipeValue;
        }
    };

    close_pipe(cin);
    close_pipe(cout);
    close_pipe(cerr);

    if (pid > 0) {
        (void)wait();
#ifdef _WIN32
        (void)CloseHandle(process_info.hProcess);
        (void)CloseHandle(process_info.hThread);
#endif
    }

    pid = 0;
    returncode = kBadReturnCode;
    args.clear();
}

#ifdef _WIN32
std::string LastErrorString() {
    LPTSTR lpMsgBuf = nullptr;
    const DWORD dw = GetLastError();

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                  dw, 0, reinterpret_cast<LPTSTR>(&lpMsgBuf), 0, nullptr);

    const std::string message = lptstr_to_string(lpMsgBuf);
    (void)LocalFree(lpMsgBuf);
    return message;
}

// Shared WaitForSingleObject result handler - eliminates duplication between poll()/wait()
void Popen::handle_wait_result(const DWORD wr) {
    switch (wr) {
    case WAIT_ABANDONED:
        throw OSError(std::format("WAIT_ABANDONED error:{}", GetLastError()));
    case WAIT_FAILED:
        throw OSError(std::format("WAIT_FAILED error:{}:{}", GetLastError(), LastErrorString()));
    case WAIT_OBJECT_0: {
        DWORD excode;
        if (!GetExitCodeProcess(process_info.hProcess, &excode))
            throw OSError(std::format("GetExitCodeProcess failed:{}:{}", GetLastError(), LastErrorString()));
        returncode = static_cast<int64_t>(excode);
        break;
    }
    default:
        throw OSError(std::format("WaitForSingleObject failed: {}", wr));
    }
}

bool Popen::poll() {
    if (this->returncode != kBadReturnCode) return true;

    const DWORD wr = WaitForSingleObject(process_info.hProcess, 0);
    if (wr == WAIT_TIMEOUT) return false;

    handle_wait_result(wr);
    return true;
}

std::optional<int64_t> Popen::try_wait(const double timeout) {
    if (this->returncode != kBadReturnCode) return this->returncode;

    const auto ms = static_cast<DWORD>(timeout < 0.0 ? INFINITE : timeout * 1000.0);
    const DWORD wr = WaitForSingleObject(process_info.hProcess, ms);

    if (wr == WAIT_TIMEOUT) return std::nullopt;

    handle_wait_result(wr);
    return this->returncode;
}

int64_t Popen::wait(const double timeout) {
    if (auto rc = try_wait(timeout)) return *rc;
    throw TimeoutExpired(std::format("timeout expired"));
}

template<typename Fn>
void for_each_child_process(const DWORD parentProcessID, Fn&& fn) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe{.dwSize = sizeof(PROCESSENTRY32)};
    for (BOOL ok = Process32First(hSnapshot, &pe); ok; ok = Process32Next(hSnapshot, &pe)) {
        if (pe.th32ParentProcessID == parentProcessID) fn(pe);
    }

    CloseHandle(hSnapshot);
}

void TerminateProcessByID(const DWORD processID) {
    if (HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processID)) {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
    }
}

bool Popen::send_signal(const SigNum signum) const {
    using enum SigNum;

    if (returncode != kBadReturnCode) return false;

    bool result;

    switch (signum) {
    case PSIGKILL: {
        result = TerminateProcess(process_info.hProcess, 137);
        for_each_child_process(process_info.dwProcessId,
                               [](const PROCESSENTRY32& pe) { TerminateProcessByID(pe.th32ProcessID); });
        break;
    }
    case PSIGINT:
        result = GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        break;
    default:
        result = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
        break;
    }

    if (!result) std::cout << "error: " << LastErrorString() << "\n";

    return result;
}
#else

#endif

bool Popen::terminate() const {
    return send_signal(SigNum::PSIGTERM);
}

bool Popen::kill() const {
    return m_soft_kill ? send_signal(SigNum::PSIGTERM) : send_signal(SigNum::PSIGKILL);
}

void Popen::ignore_cout() {
    pipe_ignore_and_close(cout);
    cout = kBadPipeValue;
}

void Popen::ignore_cerr() {
    pipe_ignore_and_close(cerr);
    cerr = kBadPipeValue;
}

void Popen::ignore_output() {
    ignore_cout();
    ignore_cerr();
}

void Popen::close_cin() {
    if (cin != kBadPipeValue) {
        (void)pipe_close(cin);
        cin = kBadPipeValue;
    }
}

std::string ProcessBuilder::windows_command() const {
    return this->command[0];
}

std::string ProcessBuilder::windows_args() const {
    return ProcessBuilder::windows_args(this->command);
}

std::string ProcessBuilder::windows_args(const CommandLine& cmd) {
    std::string args;
    for (size_t i = 0; i < cmd.size(); ++i) {
        if (i > 0) args += ' ';
#ifdef _WIN32
        args += escape_shell_arg(cmd[i], false);
#else
        args += escape_shell_arg(cmd[i]);
#endif
    }
    return args;
}

// Shared helper: spawn threads to collect cout/cerr, join them, return the threads
static void collect_outputs(Popen& popen, CompletedProcess& completed) {
    std::thread cout_thread;
    std::thread cerr_thread;

    // Lambda to collect a pipe's output and close it
    const auto collect = [](PipeHandle& handle, std::string& dest) {
        if (handle == kBadPipeValue) return;
        dest = pipe_read_all(handle);
        (void)pipe_close(handle);
        handle = kBadPipeValue;
    };

    if (popen.cout != kBadPipeValue) cout_thread = std::thread([&]() { collect(popen.cout, completed.cout); });
    if (popen.cerr != kBadPipeValue) cerr_thread = std::thread([&]() { collect(popen.cerr, completed.cerr); });

    if (cout_thread.joinable()) cout_thread.join();
    if (cerr_thread.joinable()) cerr_thread.join();
}

CompletedProcess run(Popen& popen, const bool check) {
    CompletedProcess completed;

    collect_outputs(popen, completed);

    (void)popen.wait();
    completed.returncode = popen.returncode;
    completed.args = CommandLine(popen.args.begin() + 1, popen.args.end());

    if (check)
        throw CalledProcessError{"failed to execute " + popen.args[0], popen.args, completed.returncode, completed.cout,
                                 completed.cerr};

    return completed;
}

CompletedProcess run(CommandLine command, const RunOptions& options) {
    Popen popen(command, options);
    CompletedProcess completed;

    collect_outputs(popen, completed);

    if (!popen.try_wait(options.timeout)) {
        (void)popen.send_signal(SigNum::PSIGTERM);
        (void)popen.wait();
        throw TimeoutExpired{"subprocess::run timeout reached", command, options.timeout, completed.cout,
                             completed.cerr};
    }

    completed.returncode = popen.returncode;
    completed.args = std::move(command);

    if (options.raise_on_nonzero && completed.returncode != 0)
        throw CalledProcessError{"failed to execute " + completed.args[0], completed.args, completed.returncode,
                                 completed.cout, completed.cerr};

    return completed;
}

double StopWatch::monotonic_seconds() {
    static const auto begin = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
}
} // namespace subprocess
