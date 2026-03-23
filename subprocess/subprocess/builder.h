#pragma once

#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pipe.h"
#include "pipevar.hpp"

namespace subprocess {

#ifdef _WIN32
std::string LastErrorString();
#endif

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

    void ignore_cout();
    void ignore_cerr();
    void ignore_output();

    [[nodiscard]] bool poll();
    int64_t wait(double timeout = -1.0);
    [[nodiscard]] std::optional<int64_t> try_wait(double timeout);
    [[nodiscard]] bool send_signal(SigNum signal) const;
    [[nodiscard]] bool terminate() const;
    [[nodiscard]] bool kill() const;

    void close();
    void close_cin();

    void enable_soft_kill(const bool value) { m_soft_kill = value; }

    friend ProcessBuilder;

private:
    void init(const CommandLine& cmd, RunOptions& opts);

#ifdef _WIN32
    PROCESS_INFORMATION process_info{};
    // Shared logic for poll()/wait() after WaitForSingleObject
    void handle_wait_result(DWORD wr);
#endif
    bool m_soft_kill{false};
};

class ProcessBuilder {
public:
    std::vector<PipeHandle> child_close_pipes{};

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

    [[nodiscard]] std::string windows_command() const;
    [[nodiscard]] std::string windows_args() const;
    static std::string windows_args(const CommandLine& cmd);

    [[nodiscard]] Popen run() const { return run_command(this->command); }
    [[nodiscard]] Popen run_command(const CommandLine& cmdline) const;
};

CompletedProcess run(Popen& popen, bool check = false);
CompletedProcess run(CommandLine command, const RunOptions& options = {});

/**
 * @brief Fluent builder using C++23 deducing this for perfect-forwarding chains.
 *
 * All setter methods use a single macro-generated template to eliminate
 * per-method boilerplate while preserving move semantics on rvalue chains.
 */
struct RunBuilder {
    RunOptions options{};
    CommandLine command{};

    RunBuilder() = default;
    RunBuilder(CommandLine cmd) : command(std::move(cmd)) {} // NOLINT
    RunBuilder(const std::initializer_list<std::string> cmd) : command(cmd) {}

// Macro eliminates boilerplate: each setter is identical except field name and param type.
#define SUBPROCESS_BUILDER_SETTER(name, field, ParamType)                                                              \
    template<typename Self>                                                                                            \
    auto&& name(this Self&& self, ParamType v) {                                                                       \
        self.options.field = v;                                                                                        \
        return std::forward<Self>(self);                                                                               \
    }

    SUBPROCESS_BUILDER_SETTER(raise_on_nonzero, raise_on_nonzero, bool)
    SUBPROCESS_BUILDER_SETTER(cin, cin, const PipeVar&)
    SUBPROCESS_BUILDER_SETTER(cout, cout, const PipeVar&)
    SUBPROCESS_BUILDER_SETTER(cerr, cerr, const PipeVar&)
    SUBPROCESS_BUILDER_SETTER(cwd, cwd, const std::string&)
    SUBPROCESS_BUILDER_SETTER(env, env, const EnvMap&)
    SUBPROCESS_BUILDER_SETTER(timeout, timeout, double)
    SUBPROCESS_BUILDER_SETTER(new_process_group, new_process_group, bool)
    SUBPROCESS_BUILDER_SETTER(create_no_window, create_no_window, bool)
    SUBPROCESS_BUILDER_SETTER(detached_process, detached_process, bool)

#undef SUBPROCESS_BUILDER_SETTER

    operator RunOptions() const { return options; } // NOLINT

    [[nodiscard]] CompletedProcess run() const { return subprocess::run(command, options); }

    [[nodiscard]] Popen popen() const { return {command, options}; }
};

double sleep_seconds(double seconds);

class StopWatch {
public:
    StopWatch() { start(); }

    void start() { m_start = monotonic_seconds(); }

    [[nodiscard]] double seconds() const { return monotonic_seconds() - m_start; }

private:
    static double monotonic_seconds();
    double m_start = 0.0;
};

#ifndef _WIN32
namespace details {
void throw_os_error(const char* function, int ec);
} // namespace details
#endif

} // namespace subprocess
