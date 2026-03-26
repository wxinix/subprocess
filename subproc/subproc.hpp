// MIT License
// Copyright (c) 2020-2026 Wuping Xin
// Copyright (c) 2020 benman64

#pragma once

// =============================================================================
// subproc.hpp — Modern C++23 single-header subprocess library for Windows
//
// Usage:  #include <subproc.hpp>
// =============================================================================

#include <tlhelp32.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace subproc {

// =============================================================================
// Basic types, constants, enums
// =============================================================================

using ssize_t = intptr_t;
using PipeHandle = HANDLE;
using pid_t = DWORD;

constexpr char kPathDelimiter = ';';
constexpr PipeHandle kBadPipeValue = nullptr;
constexpr int kBadReturnCode = -1000;
constexpr size_t kPipeBufferSize = 8192;

using CommandLine = std::vector<std::string>;
using EnvMap = std::map<std::string, std::string>;

enum class PipeOption : int {
    inherit,
    cout,
    cerr,
    specific,
    pipe,
    close,
    none
};

enum class SigNum {
    PSIGHUP = 1,
    PSIGINT = SIGINT,
    PSIGQUIT = 3,
    PSIGILL = SIGILL,
    PSIGTRAP = 5,
    PSIGABRT = SIGABRT,
    PSIGIOT = 6,
    PSIGBUS = 7,
    PSIGFPE = SIGFPE,
    PSIGKILL = 9,
    PSIGUSR1 = 10,
    PSIGSEGV = SIGSEGV,
    PSIGUSR2 = 12,
    PSIGPIPE = 13,
    PSIGALRM = 14,
    PSIGTERM = SIGTERM,
    PSIGSTKFLT = 16,
    PSIGCHLD = 17,
    PSIGCONT = 18,
    PSIGSTOP = 19,
    PSIGTSTP = 20,
    PSIGTTIN = 21,
    PSIGTTOU = 22,
    PSIGURG = 23,
    PSIGXCPU = 24,
    PSIGXFSZ = 25,
    PSIGVTALRM = 26,
    PSIGPROF = 27,
    PSIGWINCH = 28,
    PSIGIO = 29
};

// =============================================================================
// Exception hierarchy
// =============================================================================

struct SubprocError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct OSError : SubprocError {
    using SubprocError::SubprocError;
};

struct CommandNotFoundError : SubprocError {
    using SubprocError::SubprocError;
};

struct SpawnError : OSError {
    using OSError::OSError;
};

struct TimeoutExpired : SubprocError {
    using SubprocError::SubprocError;
    TimeoutExpired(const std::string& msg, CommandLine a_cmd, const double a_timeout, std::string a_cout,
                   std::string a_cerr)
        : SubprocError{msg}, cmd{std::move(a_cmd)}, timeout{a_timeout}, cout{std::move(a_cout)},
          cerr{std::move(a_cerr)} {}

    CommandLine cmd;
    double timeout;
    std::string cout;
    std::string cerr;
};

struct CalledProcessError : SubprocError {
    using SubprocError::SubprocError;
    CalledProcessError(const std::string& msg, CommandLine a_cmd, const int64_t retcode, std::string a_cout,
                       std::string a_cerr)
        : SubprocError{msg}, returncode{retcode}, cmd{std::move(a_cmd)}, cout{std::move(a_cout)},
          cerr{std::move(a_cerr)} {}

    int64_t returncode;
    CommandLine cmd;
    std::string cout;
    std::string cerr;
};

struct CompletedProcess {
    CommandLine args;
    int64_t returncode = -1;
    std::string cout;
    std::string cerr;
    explicit operator bool() const { return returncode == 0; }
};

// =============================================================================
// PipeVar — variant-based pipe redirection
// =============================================================================

template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

using PipeVar = std::variant<PipeOption, std::string, PipeHandle, std::istream*, std::ostream*, FILE*>;

inline PipeOption get_pipe_option(const PipeVar& var) {
    return std::visit(overloaded{
                          [](const PipeOption opt) { return opt; },
                          [](PipeHandle) { return PipeOption::specific; },
                          [](const auto&) { return PipeOption::pipe; },
                      },
                      var);
}

// =============================================================================
// UTF-8 / UTF-16 conversion
// =============================================================================

template<typename CharT>
    requires(std::same_as<CharT, char16_t> || std::same_as<CharT, wchar_t>)
size_t strlen16(const CharT* input) {
    size_t size = 0;
    for (; *input; ++input)
        ++size;
    return size;
}

inline std::u16string utf8_to_utf16(const std::string& input) {
    if (input.empty()) return {};
    const auto size = static_cast<int>(input.size() + 1);

    // Stack buffer for typical short strings; heap fallback for long ones.
    constexpr int kStackBufSize = 256;
    wchar_t stack_buf[kStackBufSize];
    wchar_t* buf = (size <= kStackBufSize) ? stack_buf : nullptr;
    std::unique_ptr<wchar_t[]> heap_buf;
    if (!buf) {
        heap_buf = std::make_unique<wchar_t[]>(static_cast<size_t>(size));
        buf = heap_buf.get();
    }

    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), size, buf, size);
    return (n > 0) ? std::u16string{buf, buf + n - 1} : std::u16string{};
}

#ifdef __MINGW32__
constexpr int WC_ERR_INVALID_CHARS = 0;
#endif

inline std::string utf16_to_utf8(const std::u16string& input) {
    if (input.empty()) return {};
    const auto size = input.size() + 1;
    const auto* wide = reinterpret_cast<const wchar_t*>(input.c_str());
    const int r =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, static_cast<int>(size), nullptr, 0, nullptr, nullptr);
    if (r <= 0) return {};
    const auto buffer = std::make_unique<char[]>(static_cast<size_t>(r));
    const int m = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(size), buffer.get(), r, nullptr, nullptr);
    return (m > 0) ? std::string{buffer.get(), buffer.get() + m - 1} : std::string{};
}

inline std::string utf16_to_utf8(const std::wstring& input) {
    return utf16_to_utf8(std::u16string{reinterpret_cast<const char16_t*>(input.c_str()), input.size()});
}

inline std::string lptstr_to_string(LPTSTR input) { // NOLINT
    if (!input) return {};
#ifdef UNICODE
    return utf16_to_utf8(input);
#else
    return input;
#endif
}

// =============================================================================
// StopWatch (defined early — used by pipe_wait_for_read)
// =============================================================================

class StopWatch {
public:
    StopWatch() { start(); }
    void start() { m_start = monotonic_seconds(); }
    [[nodiscard]] double seconds() const { return monotonic_seconds() - m_start; }

private:
    static double monotonic_seconds() {
        static const auto begin = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    }
    double m_start = 0.0;
};

// =============================================================================
// Pipe types and operations
// =============================================================================

struct PipePair {
    PipePair() = default;
    PipePair(const PipeHandle input, const PipeHandle output) : input(input), output(output) {}
    ~PipePair() { close(); }

    PipePair(const PipePair&) = delete;
    PipePair& operator=(const PipePair&) = delete;
    PipePair(PipePair&& other) noexcept : input(other.input), output(other.output) {
        other.disown();
    }

    PipePair& operator=(PipePair&& other) noexcept {
        if (this == &other) return *this;
        close();
        input = other.input;
        output = other.output;
        other.disown();
        return *this;
    }

    PipeHandle input = kBadPipeValue;
    PipeHandle output = kBadPipeValue;

    void disown() {
        input = kBadPipeValue;
        output = kBadPipeValue;
    }

    void close() {
        close_input();
        close_output();
    }

    void close_input() { close_handle(&PipePair::input); }
    void close_output() { close_handle(&PipePair::output); }
    explicit operator bool() const noexcept { return input != kBadPipeValue || output != kBadPipeValue; }

private:
    void close_handle(PipeHandle PipePair::* member) {
        if (this->*member != kBadPipeValue) {
            CloseHandle(this->*member);
            this->*member = kBadPipeValue;
        }
    }
};

inline bool pipe_close(const PipeHandle handle) {
    if (handle == kBadPipeValue) return false;
    return CloseHandle(handle) != 0;
}

inline void close_and_reset(PipeHandle& h) {
    if (h != kBadPipeValue) {
        (void)pipe_close(h);
        h = kBadPipeValue;
    }
}

[[nodiscard]] inline PipePair pipe_create(const bool inheritable = true) {
    SECURITY_ATTRIBUTES security{.nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = inheritable};
    PipeHandle input, output;
    if (!CreatePipe(&input, &output, &security, 0)) throw OSError("could not create pipe");
    return {input, output};
}

inline void pipe_set_inheritable(const PipeHandle handle, const bool inheritable) {
    if (handle == kBadPipeValue) throw std::invalid_argument("pipe_set_inheritable: handle is invalid");
    if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, inheritable ? HANDLE_FLAG_INHERIT : 0))
        throw OSError("SetHandleInformation failed");
}

[[nodiscard]] inline ssize_t pipe_read(const PipeHandle handle, void* buffer, const size_t size) {
    DWORD bread = 0;
    return ReadFile(handle, buffer, static_cast<DWORD>(size), &bread, nullptr) ? static_cast<ssize_t>(bread) : -1;
}

[[nodiscard]] inline ssize_t pipe_write(const PipeHandle handle, const void* buffer, const size_t size) {
    DWORD written = 0;
    return WriteFile(handle, buffer, static_cast<DWORD>(size), &written, nullptr) ? static_cast<ssize_t>(written) : -1;
}

[[nodiscard]] inline std::string pipe_read_all(const PipeHandle handle) {
    std::string result;
    if (handle == kBadPipeValue) return result;
    std::array<char, kPipeBufferSize> buf{};
    for (ssize_t n = pipe_read(handle, buf.data(), buf.size()); n > 0; n = pipe_read(handle, buf.data(), buf.size()))
        result.append(buf.data(), static_cast<size_t>(n));
    return result;
}

[[nodiscard]] inline ssize_t pipe_peek_bytes(const PipeHandle handle) {
    DWORD available = 0;
    return PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) ? static_cast<ssize_t>(available) : -1;
}

[[nodiscard]] inline ssize_t pipe_read_some(const PipeHandle handle, void* buffer, const size_t size) {
    if (size == 0) return 0;
    const ssize_t first = pipe_read(handle, buffer, 1);
    if (first <= 0) return first;
    const ssize_t available = pipe_peek_bytes(handle);
    if (available <= 0) return 1;
    auto* cursor = static_cast<uint8_t*>(buffer) + 1;
    const auto to_read = (std::min)(static_cast<ssize_t>(size - 1), available);
    const ssize_t more = pipe_read(handle, cursor, static_cast<size_t>(to_read));
    return (more < 0) ? more : 1 + more;
}

[[nodiscard]] inline ssize_t pipe_write_fully(const PipeHandle handle, const void* buffer, const size_t size) {
    auto* cursor = static_cast<const uint8_t*>(buffer);
    ssize_t total = 0;
    while (static_cast<size_t>(total) < size) {
        const ssize_t n = pipe_write(handle, cursor, size - static_cast<size_t>(total));
        if (n < 0) return -(total + 1);
        if (n == 0) break;
        cursor += n;
        total += n;
    }
    return total;
}

[[nodiscard]] inline bool pipe_set_blocking(const PipeHandle handle, const bool should_block) {
    DWORD state = 0;
    if (!GetNamedPipeHandleStateA(handle, &state, nullptr, nullptr, nullptr, nullptr, 0)) return false;
    if (should_block)
        state &= ~PIPE_NOWAIT;
    else
        state |= PIPE_NOWAIT;
    return SetNamedPipeHandleState(handle, &state, nullptr, nullptr) != 0;
}

[[nodiscard]] inline int pipe_wait_for_read(const PipeHandle handle, const double seconds) {
    struct BlockingGuard {
        const PipeHandle h;
        DWORD original_state = 0;
        bool saved = false;
        explicit BlockingGuard(const PipeHandle handle) : h(handle) {
            if (GetNamedPipeHandleStateA(h, &original_state, nullptr, nullptr, nullptr, nullptr, 0)) {
                saved = true;
                DWORD blocking = original_state & ~PIPE_NOWAIT;
                (void)SetNamedPipeHandleState(h, &blocking, nullptr, nullptr);
            }
        }
        ~BlockingGuard() {
            if (saved) (void)SetNamedPipeHandleState(h, &original_state, nullptr, nullptr);
        }
    };

    const BlockingGuard guard(handle);
    const StopWatch watch;
    double remaining = seconds;
    while (true) {
        const auto dw_timeout = static_cast<DWORD>(remaining < 0 ? INFINITE : remaining * 1000.0);
        const DWORD result = WaitForSingleObject(handle, dw_timeout);
        if (result == WAIT_OBJECT_0) {
            DWORD available = 0;
            if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) return -1;
            if (available > 0) return 1;
            if (seconds < 0) continue;  // infinite timeout: retry
            remaining = seconds - watch.seconds();
            if (remaining <= 0) return 0;  // time exhausted on spurious wake
            continue;
        }
        if (result == WAIT_TIMEOUT) return 0;
        return -1;
    }
}

[[nodiscard]] inline PipeHandle pipe_file(const std::string_view filename, const std::string_view mode) {
    DWORD access = 0, disposition = 0;
    if (mode.contains('r')) {
        access |= GENERIC_READ;
        disposition = OPEN_EXISTING;
    }

    if (mode.contains('w')) {
        access |= GENERIC_WRITE;
        disposition = CREATE_ALWAYS;
    }

    if (mode.contains('+')) access |= GENERIC_READ | GENERIC_WRITE;
    const std::u16string wide = utf8_to_utf16(std::string{filename});
    HANDLE hFile = CreateFileW(reinterpret_cast<LPCWSTR>(wide.c_str()), access, 0, nullptr, disposition,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    return (hFile != INVALID_HANDLE_VALUE) ? hFile : kBadPipeValue;
}

inline void pipe_ignore_and_close(const PipeHandle handle) {
    if (handle == kBadPipeValue) return;
    std::thread([handle]() {
        std::array<uint8_t, kPipeBufferSize> buffer{};
        while (pipe_read(handle, buffer.data(), buffer.size()) >= 0) {}
        (void)pipe_close(handle);
    }).detach();
}

// =============================================================================
// Shell utilities (defined before detail — detail helpers depend on these)
// =============================================================================

[[nodiscard]] inline std::string getenv(const std::string& name) {
    char* value = nullptr;
    size_t len = 0;
    (void)_dupenv_s(&value, &len, name.c_str());
    const std::string result = value ? std::string{value} : std::string{};
    free(value);
    return result;
}

[[nodiscard]] inline std::string get_cwd() {
    return std::filesystem::current_path().string();
}
inline void set_cwd(const std::string& path) {
    std::filesystem::current_path(path);
}

[[nodiscard]] inline std::string escape_shell_arg(const std::string& arg, const bool escape = true) {
    if (!escape) return arg;

    const bool needs_quote = std::ranges::any_of(arg, [](const char c) {
        return !std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-' && c != '+' && c != '/';
    });

    if (!needs_quote) return arg;

    std::string result;
    result.reserve(arg.size() + 4);
    result += '\"';

    for (const char ch : arg) {
        if (ch == '\"' || ch == '\\') result += '\\';
        result += ch;
    }

    result += '\"';
    return result;
}

// =============================================================================
// detail — internal helpers (fully defined, not part of the public API)
// =============================================================================

namespace detail {

// --- RAII pipe closer for background threads --------------------------------

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

// --- Pipe threading helpers -------------------------------------------------

template<typename OutputSink>
    requires(std::same_as<OutputSink, std::ostream*> || std::same_as<OutputSink, FILE*>)
inline void pipe_reader_thread(const PipeHandle input, const OutputSink output) {
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

inline void pipe_thread(FILE* input, const PipeHandle output, const bool autoclose) {
    std::thread([=]() {
        const AutoClosePipe guard(output, autoclose);
        std::array<char, kPipeBufferSize> buffer{};
        for (size_t n; (n = fread(buffer.data(), 1, buffer.size(), input)) > 0;)
            (void)pipe_write(output, buffer.data(), n);
    }).detach();
}

inline void pipe_thread(const std::string& input, const PipeHandle output, const bool autoclose) {
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

inline void pipe_thread(std::istream* input, const PipeHandle output, const bool autoclose) {
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

// --- Redirect stream dispatch -----------------------------------------------

inline void setup_redirect_stream(const PipeHandle input, PipeVar& output) {
    std::visit(overloaded{
                   [](std::istream*) { throw std::domain_error("expected something to output to"); },
                   [input](std::ostream* os) { pipe_reader_thread(input, os); },
                   [input](FILE* f) { pipe_reader_thread(input, f); },
                   [](const auto&) {},
               },
               output);
}

inline bool setup_redirect_stream(PipeVar& input, const PipeHandle output) {
    return std::visit(
        overloaded{
            [](PipeOption) { return false; },
            [](PipeHandle) { return false; },
            [](std::ostream*) -> bool { throw std::domain_error("reading from std::ostream doesn't make sense"); },
            [output](auto&& source) {
                pipe_thread(source, output, true);
                return true;
            },
        },
        input);
}

// --- Output collection ------------------------------------------------------

inline void collect_outputs(auto& popen, CompletedProcess& completed) {
    std::thread cout_thread, cerr_thread;

    const auto collect = [](PipeHandle& handle, std::string& dest) {
        if (handle == kBadPipeValue) return;
        dest = pipe_read_all(handle);
        close_and_reset(handle);
    };

    if (popen.cout != kBadPipeValue) cout_thread = std::thread([&] { collect(popen.cout, completed.cout); });
    if (popen.cerr != kBadPipeValue) cerr_thread = std::thread([&] { collect(popen.cerr, completed.cerr); });
    if (cout_thread.joinable()) cout_thread.join();
    if (cerr_thread.joinable()) cerr_thread.join();
}

// --- Windows process helpers ------------------------------------------------

template<typename Fn>
inline void for_each_child_process(const DWORD parentProcessID, Fn&& fn) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe{.dwSize = sizeof(PROCESSENTRY32)};

    for (BOOL ok = Process32First(hSnapshot, &pe); ok; ok = Process32Next(hSnapshot, &pe))
        if (pe.th32ParentProcessID == parentProcessID) fn(pe);

    CloseHandle(hSnapshot);
}

inline void TerminateProcessByID(const DWORD processID) {
    if (HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processID)) {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
    }
}

// --- Path / program-finding helpers -----------------------------------------

constexpr bool is_drive(const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline std::string clean_path(std::string path) {
    std::ranges::replace(path, '\\', '/');
    if (path.size() == 2 && is_drive(path[0]) && path[1] == ':') path += '/';
    while (path.ends_with("//"))
        path.pop_back();
    return path;
}

inline bool is_absolute_path(const std::string& path) {
    return path.size() >= 2 && is_drive(path[0]) && path[1] == ':';
}

inline bool is_file(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

inline std::string join_path(std::string parent, std::string child) {
    parent = clean_path(parent);
    child = clean_path(child);
    if (parent.empty() || child.empty() || child == ".") return parent;

    while (child.starts_with("./"))
        child.erase(0, 2);

    if (parent.back() != '/') parent += '/';
    if (child.front() == '/') child.erase(0, 1);

    return parent + child;
}

inline std::vector<std::string> split_path(const std::string& s) {
    if (s.empty()) return {};
    return s | std::views::split(kPathDelimiter) |
           std::views::transform([](auto&& r) { return std::string(r.begin(), r.end()); }) |
           std::ranges::to<std::vector>();
}

inline const std::vector<std::string>& get_pathext() {
    static const auto extensions = [] {
        auto ext_str = getenv("PATHEXT");
        if (ext_str.empty()) ext_str = ".exe";
        return split_path(ext_str);
    }();
    return extensions;
}

inline std::string try_exe(const std::string& path) {
    if (is_file(path)) return path;
    for (const auto& ext : get_pathext()) {
        if (ext.empty()) continue;
        if (const auto test_path = path + ext; is_file(test_path)) return test_path;
    }
    return {};
}

inline std::mutex g_program_cache_mutex;
inline std::map<std::string, std::string> g_program_cache;

inline std::string make_abspath(std::string dir, std::string relative = "") {
    dir = clean_path(dir);
    if (is_absolute_path(dir)) return dir;
    if (relative.empty()) relative = get_cwd();
    if (!is_absolute_path(relative)) relative = join_path(get_cwd(), relative);
    return join_path(relative, dir);
}

inline std::string find_program_in_path(const std::string& name) {
    if (name.empty()) return {};

    // Absolute/relative paths bypass the cache — no lock needed.
    if (name.size() >= 2 && (is_absolute_path(name) || name.starts_with("./") || name[0] == '/')) {
        if (is_file(name)) return make_abspath(name);
        if (const auto p = try_exe(name); !p.empty() && is_file(p)) return make_abspath(p);
    }

    // Check cache under lock.
    {
        std::scoped_lock lock(g_program_cache_mutex);
        if (const auto it = g_program_cache.find(name); it != g_program_cache.end())
            return it->second;
    }

    // Search PATH without holding the lock (filesystem I/O).
    for (const auto& dir : split_path(getenv("PATH"))) {
        if (dir.empty()) continue;
        if (const auto p = try_exe(std::format("{}/{}", dir, name)); !p.empty() && is_file(p)) {
            std::scoped_lock lock(g_program_cache_mutex);
            g_program_cache[name] = p;
            return p;
        }
    }
    return {};
}

// --- Environment parsing helper ---------------------------------------------

inline void parse_env_entry(const std::string_view entry, EnvMap& out) {
    if (const auto eq = entry.find('='); eq != std::string_view::npos && eq > 0)
        out[std::string(entry.substr(0, eq))] = std::string(entry.substr(eq + 1));
}

inline std::string last_error_string() {
    LPTSTR lpMsgBuf = nullptr;
    const DWORD dw = GetLastError();
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                  dw, 0, reinterpret_cast<LPTSTR>(&lpMsgBuf), 0, nullptr);
    const std::string message = lptstr_to_string(lpMsgBuf);
    (void)LocalFree(lpMsgBuf);
    return message;
}

} // namespace detail

// =============================================================================
// Shell utilities (continued) — abspath needs detail::clean_path/join_path
// =============================================================================

[[nodiscard]] inline std::string abspath(std::string dir, std::string relative = "") {
    return detail::make_abspath(std::move(dir), std::move(relative));
}

// =============================================================================
// Environment management
// =============================================================================

inline void find_program_clear_cache() {
    std::scoped_lock lock(detail::g_program_cache_mutex);
    detail::g_program_cache.clear();
}

class EnvironSetter {
public:
    EnvironSetter(std::string name) : m_name(std::move(name)) {} // NOLINT

    operator std::string() const { return to_string(); } // NOLINT
    explicit operator bool() const { return !m_name.empty() && !getenv(m_name).empty(); }
    [[nodiscard]] std::string to_string() const { return getenv(m_name); }

    EnvironSetter& operator=(const std::string& str) { return *this = str.c_str(); }

    EnvironSetter& operator=(const char* str) {
        if (m_name == "PATH" || m_name == "Path" || m_name == "path") find_program_clear_cache();
        (void)_putenv_s(m_name.c_str(), str ? str : "");
        return *this;
    }

    EnvironSetter& operator=(std::nullptr_t) { return *this = static_cast<const char*>(nullptr); }

    template<typename T>
        requires std::is_arithmetic_v<T>
    EnvironSetter& operator=(const T value) {
        if constexpr (std::is_same_v<T, bool>)
            return *this = std::string{value ? "1" : "0"};
        else
            return *this = std::to_string(value);
    }

private:
    const std::string m_name;
};

struct EnvLock {
    EnvLock() : m_guard(mtx) {}

private:
    static inline std::mutex mtx{};
    std::scoped_lock<std::mutex> m_guard;
};

class Environ {
public:
    EnvironSetter operator[](const std::string& name) const { return EnvironSetter{name}; }
};

inline Environ cenv;

[[nodiscard]] inline EnvMap current_env_copy() {
    EnvMap result;
    const auto env_block = GetEnvironmentStringsW();
    if (!env_block) return result;
    for (auto list = env_block; *list; list += strlen16(list) + 1)
        detail::parse_env_entry(utf16_to_utf8(list), result);
    (void)FreeEnvironmentStringsW(env_block);
    return result;
}

[[nodiscard]] inline std::u16string create_env_block(const EnvMap& map) {
    std::u16string result;
    size_t size = 1;
    for (const auto& [name, value] : map)
        size += name.size() + value.size() + 2;
    result.reserve(size * 2);
    for (const auto& [name, value] : map) {
        result += utf8_to_utf16(std::format("{}={}", name, value));
        result += char16_t{'\0'};
    }
    result += char16_t{'\0'};
    return result;
}

class CwdGuard {
public:
    CwdGuard() : m_cwd(get_cwd()) {}
    ~CwdGuard() { set_cwd(m_cwd); }
    CwdGuard(const CwdGuard&) = delete;
    CwdGuard& operator=(const CwdGuard&) = delete;

private:
    const std::string m_cwd;
};

class EnvGuard : public CwdGuard {
public:
    EnvGuard() : m_env(current_env_copy()) {}

    ~EnvGuard() {
        const auto new_env = current_env_copy();
        for (const auto& name : new_env | std::views::keys)
            if (!m_env.contains(name)) cenv[name] = nullptr;
        for (const auto& [name, value] : m_env)
            if (!new_env.contains(name) || value != new_env.at(name)) cenv[name] = value;
    }

    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

private:
    const EnvMap m_env;
};

// =============================================================================
// sleep
// =============================================================================

inline double sleep_seconds(const double seconds) {
    const StopWatch watch;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    return watch.seconds();
}

// =============================================================================
// RunOptions, Popen, ProcessBuilder, RunBuilder, run()
// =============================================================================

struct RunOptions {
    PipeVar cin{PipeOption::inherit};
    PipeVar cout{PipeOption::inherit};
    PipeVar cerr{PipeOption::inherit};
    bool create_no_window{false};
    bool detached_process{false};
    bool new_process_group{false};
    std::string cwd{};
    double timeout{-1};
    bool raise_on_nonzero{false};
    EnvMap env{};
};

class ProcessBuilder;

struct Popen {
    Popen() = default;
    Popen(const CommandLine& command, RunOptions options);
    Popen(const Popen&) = delete;
    Popen& operator=(const Popen&) = delete;
    Popen(Popen&&) noexcept;
    Popen& operator=(Popen&&) noexcept;
    ~Popen();

    PipeHandle cin{kBadPipeValue};
    PipeHandle cout{kBadPipeValue};
    PipeHandle cerr{kBadPipeValue};
    pid_t pid{0};
    int64_t returncode{kBadReturnCode};
    CommandLine args{};

    void ignore_cout() {
        pipe_ignore_and_close(cout);
        cout = kBadPipeValue;
    }
    void ignore_cerr() {
        pipe_ignore_and_close(cerr);
        cerr = kBadPipeValue;
    }
    void ignore_output() {
        ignore_cout();
        ignore_cerr();
    }
    [[nodiscard]] bool poll();
    int64_t wait(double timeout = -1.0);
    [[nodiscard]] std::optional<int64_t> try_wait(double timeout);
    [[nodiscard]] bool send_signal(SigNum signum) const;
    [[nodiscard]] bool terminate() const { return send_signal(SigNum::PSIGTERM); }
    [[nodiscard]] bool kill() const {
        return m_soft_kill ? send_signal(SigNum::PSIGTERM) : send_signal(SigNum::PSIGKILL);
    }
    void close();
    void close_cin() { close_and_reset(cin); }
    void enable_soft_kill(const bool value) { m_soft_kill = value; }

    friend ProcessBuilder;

private:
    void init(const CommandLine& command, RunOptions& options);
    PROCESS_INFORMATION process_info{};
    void handle_wait_result(DWORD wr);
    bool m_soft_kill{false};
};

// Internal — used by Popen::init() to configure and launch processes.
class ProcessBuilder {
public:
    PipeOption cerr_option{PipeOption::inherit};
    PipeHandle cerr_pipe{kBadPipeValue};
    PipeOption cin_option{PipeOption::inherit};
    PipeHandle cin_pipe{kBadPipeValue};
    PipeOption cout_option{PipeOption::inherit};
    PipeHandle cout_pipe{kBadPipeValue};
    bool create_no_window{false};
    bool detached_process{false};
    bool new_process_group{false};
    CommandLine command{};
    EnvMap env{};
    std::string cwd{};

    [[nodiscard]] std::string windows_command() const {
        if (command.empty()) throw std::invalid_argument("ProcessBuilder: command is empty");
        return command[0];
    }
    [[nodiscard]] std::string windows_args() const { return windows_args(command); }

    static std::string windows_args(const CommandLine& cmd) {
        std::string args;
        for (size_t i = 0; i < cmd.size(); ++i) {
            if (i > 0) args += ' ';
            args += cmd[i];
        }
        return args;
    }

    [[nodiscard]] Popen run() const { return run_command(this->command); }
    [[nodiscard]] Popen run_command(const CommandLine& cmdline) const;
};

[[nodiscard]] CompletedProcess run(Popen& popen, bool check = false);
[[nodiscard]] CompletedProcess run(CommandLine command, const RunOptions& options = {});

struct RunBuilder {
    RunOptions options{};
    CommandLine command{};

    RunBuilder() = default;
    RunBuilder(CommandLine cmd) : command(std::move(cmd)) {} // NOLINT
    RunBuilder(const std::initializer_list<std::string> cmd) : command(cmd) {}

#define SUBPROC_BUILDER_SETTER(name, field, ParamType)                                                                 \
    template<typename Self>                                                                                            \
    auto&& name(this Self&& self, ParamType v) {                                                                       \
        self.options.field = v;                                                                                        \
        return std::forward<Self>(self);                                                                               \
    }

    SUBPROC_BUILDER_SETTER(raise_on_nonzero, raise_on_nonzero, bool)
    SUBPROC_BUILDER_SETTER(cin, cin, const PipeVar&)
    SUBPROC_BUILDER_SETTER(cout, cout, const PipeVar&)
    SUBPROC_BUILDER_SETTER(cerr, cerr, const PipeVar&)
    SUBPROC_BUILDER_SETTER(cwd, cwd, const std::string&)
    SUBPROC_BUILDER_SETTER(env, env, const EnvMap&)
    SUBPROC_BUILDER_SETTER(timeout, timeout, double)
    SUBPROC_BUILDER_SETTER(new_process_group, new_process_group, bool)
    SUBPROC_BUILDER_SETTER(create_no_window, create_no_window, bool)
    SUBPROC_BUILDER_SETTER(detached_process, detached_process, bool)

#undef SUBPROC_BUILDER_SETTER

    operator RunOptions() const { return options; } // NOLINT
    [[nodiscard]] CompletedProcess run() const { return subproc::run(command, options); }
    [[nodiscard]] Popen popen() const { return {command, options}; }
};

// =============================================================================
// find_program (defined after Popen — python3 detection creates a Popen)
// =============================================================================

[[nodiscard]] inline std::string find_program(const std::string& name) {
    if (name != "python3") return detail::find_program_in_path(name);
    const auto result = detail::find_program_in_path("python");
    if (result.empty()) return {};
    Popen p(CommandLine{result, "--version"}, {.cout = PipeOption::pipe, .cerr = PipeOption::cout});
    const auto output = pipe_read_all(p.cout);
    (void)p.wait();
    return output.contains("3.") ? result : std::string{};
}

// =============================================================================
// Popen / ProcessBuilder / run() — out-of-class implementations
// =============================================================================

inline Popen::Popen(const CommandLine& command, RunOptions options) {
    init(command, options);
}

inline void Popen::init(const CommandLine& command, RunOptions& options) {
    ProcessBuilder builder;

    const auto set_pipe = [](PipeHandle& pipe, PipeOption& opt, const PipeVar& var, const char* err) {
        opt = get_pipe_option(var);
        if (opt == PipeOption::specific) {
            pipe = std::get<PipeHandle>(var);
            if (pipe == kBadPipeValue) throw std::invalid_argument(err);
        }
    };

    set_pipe(builder.cin_pipe, builder.cin_option, options.cin, "Bad pipe value for cin");
    set_pipe(builder.cout_pipe, builder.cout_option, options.cout, "Bad pipe value for cout");
    set_pipe(builder.cerr_pipe, builder.cerr_option, options.cerr, "Bad pipe value for cerr");
    builder.new_process_group = options.new_process_group;
    builder.create_no_window = options.create_no_window;
    builder.detached_process = options.detached_process;
    builder.env = std::move(options.env);
    builder.cwd = std::move(options.cwd);
    *this = builder.run_command(command);
    if (detail::setup_redirect_stream(options.cin, this->cin)) this->cin = kBadPipeValue;
    detail::setup_redirect_stream(this->cout, options.cout);
    detail::setup_redirect_stream(this->cerr, options.cerr);
}

inline Popen::Popen(Popen&& other) noexcept {
    *this = std::move(other);
}

inline Popen& Popen::operator=(Popen&& other) noexcept {
    if (this == &other) return *this;
    close();
    cin = other.cin;
    cout = other.cout;
    cerr = other.cerr;
    pid = other.pid;
    returncode = other.returncode;
    args = std::move(other.args);
    m_soft_kill = other.m_soft_kill;
    process_info = other.process_info;

    other.process_info = {};
    other.cin = kBadPipeValue;
    other.cout = kBadPipeValue;
    other.cerr = kBadPipeValue;
    other.pid = 0;
    other.returncode = kBadReturnCode;
    return *this;
}

inline Popen::~Popen() {
    close();
}

inline void Popen::close() {
    close_and_reset(cin);
    close_and_reset(cout);
    close_and_reset(cerr);

    if (pid > 0) {
        try {
            (void)wait();
        } catch (...) {
            // Suppress exceptions — close() is called from destructor and noexcept move.
        }
        (void)CloseHandle(process_info.hProcess);
        (void)CloseHandle(process_info.hThread);
        process_info = {};
    }

    pid = 0;
    returncode = kBadReturnCode;
    args.clear();
}

inline void Popen::handle_wait_result(const DWORD wr) {
    switch (wr) {
    case WAIT_ABANDONED:
        throw OSError(std::format("WAIT_ABANDONED error:{}", GetLastError()));
    case WAIT_FAILED:
        throw OSError(std::format("WAIT_FAILED error:{}:{}", GetLastError(), detail::last_error_string()));
    case WAIT_OBJECT_0: {
        DWORD excode;
        if (!GetExitCodeProcess(process_info.hProcess, &excode))
            throw OSError(std::format("GetExitCodeProcess failed:{}:{}", GetLastError(), detail::last_error_string()));
        returncode = static_cast<int64_t>(excode);
        break;
    }
    default:
        throw OSError(std::format("WaitForSingleObject failed: {}", wr));
    }
}

inline bool Popen::poll() {
    if (returncode != kBadReturnCode) return true;
    const DWORD wr = WaitForSingleObject(process_info.hProcess, 0);
    if (wr == WAIT_TIMEOUT) return false;
    handle_wait_result(wr);
    return true;
}

inline std::optional<int64_t> Popen::try_wait(const double timeout) {
    if (returncode != kBadReturnCode) return returncode;
    const auto ms = static_cast<DWORD>(timeout < 0.0 ? INFINITE : timeout * 1000.0);
    const DWORD wr = WaitForSingleObject(process_info.hProcess, ms);
    if (wr == WAIT_TIMEOUT) return std::nullopt;
    handle_wait_result(wr);
    return returncode;
}

inline int64_t Popen::wait(const double timeout) {
    if (const auto rc = try_wait(timeout)) return *rc;
    throw TimeoutExpired("timeout expired", args, timeout, {}, {});
}

inline bool Popen::send_signal(const SigNum signum) const {
    using enum SigNum;
    if (returncode != kBadReturnCode) return false;
    bool result;
    switch (signum) {
    case PSIGKILL:
        result = TerminateProcess(process_info.hProcess, 137);
        detail::for_each_child_process(
            process_info.dwProcessId, [](const PROCESSENTRY32& pe) { detail::TerminateProcessByID(pe.th32ProcessID); });
        break;
    case PSIGINT:
        result = GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        break;
    default:
        result = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
        break;
    }
    if (!result) std::cerr << "error: " << detail::last_error_string() << "\n";
    return result;
}

inline Popen ProcessBuilder::run_command(const CommandLine& cmdline) const {
    using enum PipeOption;
    const std::string program = find_program(cmdline[0]);
    if (program.empty()) throw CommandNotFoundError(std::format("Command \"{}\" not found.", cmdline[0]));

    Popen process{};
    PipePair cin_pair, cout_pair, cerr_pair;

    PROCESS_INFORMATION piProcInfo{};
    STARTUPINFO siStartInfo{};
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    siStartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    siStartInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    const auto configure_pipe = [](const PipeOption option, const PipeHandle specific_pipe, PipePair& pair,
                                   HANDLE& std_handle, PipeHandle& process_handle, const bool is_input) {
        switch (option) {
        case close:
        case pipe: {
            pair = pipe_create();
            const auto child_end = is_input ? pair.input : pair.output;
            const auto parent_end = is_input ? pair.output : pair.input;
            std_handle = child_end;
            (void)SetHandleInformation(parent_end, HANDLE_FLAG_INHERIT, 0);
            if (option == pipe) process_handle = parent_end;
            break;
        }
        case specific:
            pipe_set_inheritable(specific_pipe, true);
            std_handle = specific_pipe;
            break;
        case none:
            std_handle = nullptr;
            break;
        default:
            break;
        }
    };

    configure_pipe(cin_option, cin_pipe, cin_pair, siStartInfo.hStdInput, process.cin, true);
    configure_pipe(cout_option, cout_pipe, cout_pair, siStartInfo.hStdOutput, process.cout, false);
    configure_pipe(cerr_option, cerr_pipe, cerr_pair, siStartInfo.hStdError, process.cerr, false);

    if (cerr_option == cout) siStartInfo.hStdError = siStartInfo.hStdOutput;
    if (cout_option == cerr) siStartInfo.hStdOutput = siStartInfo.hStdError;

    const char* const l_cwd = this->cwd.empty() ? nullptr : this->cwd.c_str();
    std::string args = windows_args(cmdline);

    void* l_env = nullptr;
    std::u16string envblock;
    if (!this->env.empty()) {
        envblock = create_env_block(this->env);
        l_env = envblock.data();
    }

    const DWORD process_flags = CREATE_UNICODE_ENVIRONMENT | (this->new_process_group ? CREATE_NEW_PROCESS_GROUP : 0) |
                                (this->create_no_window ? CREATE_NO_WINDOW : 0) |
                                (this->detached_process ? DETACHED_PROCESS : 0);

    const BOOL bSuccess = CreateProcess(program.c_str(), args.data(), nullptr, nullptr, TRUE, process_flags, l_env,
                                        l_cwd, &siStartInfo, &piProcInfo);

    process.process_info = piProcInfo;
    process.pid = piProcInfo.dwProcessId;

    const auto cleanup_pair = [](PipePair& pair, const PipeOption option, const bool is_input) {
        if (pair) is_input ? pair.close_input() : pair.close_output();
        if (option == close) pair.close();
        pair.disown();
    };

    cleanup_pair(cin_pair, cin_option, true);
    cleanup_pair(cout_pair, cout_option, false);
    cleanup_pair(cerr_pair, cerr_option, false);

    process.args = cmdline;
    if (!bSuccess) throw SpawnError(std::format("CreateProcess failed: {}", detail::last_error_string()));
    return process;
}

[[nodiscard]] inline CompletedProcess run(Popen& popen, const bool check) {
    CompletedProcess completed;
    detail::collect_outputs(popen, completed);
    (void)popen.wait();
    completed.returncode = popen.returncode;
    completed.args = popen.args.size() > 1
                         ? CommandLine(popen.args.begin() + 1, popen.args.end())
                         : CommandLine{};
    if (check)
        throw CalledProcessError{"failed to execute " + popen.args[0], popen.args, completed.returncode, completed.cout,
                                 completed.cerr};
    return completed;
}

[[nodiscard]] inline CompletedProcess run(CommandLine command, const RunOptions& options) {
    Popen popen(command, options);
    CompletedProcess completed;
    detail::collect_outputs(popen, completed);

    if (!popen.try_wait(options.timeout)) {
        (void)popen.send_signal(SigNum::PSIGTERM);
        (void)popen.wait();
        throw TimeoutExpired{"subproc::run timeout reached", command, options.timeout, completed.cout, completed.cerr};
    }

    completed.returncode = popen.returncode;
    completed.args = std::move(command);
    if (options.raise_on_nonzero && completed.returncode != 0)
        throw CalledProcessError{"failed to execute " + completed.args[0], completed.args, completed.returncode,
                                 completed.cout, completed.cerr};
    return completed;
}

} // namespace subproc
