#ifdef _WIN32

#include "builder.h"

#include <windows.h>

#include <format>

#include "environ.h"
#include "shellutils.h"

namespace subprocess {

Popen ProcessBuilder::run_command(const CommandLine& cmdline) const {
    using enum PipeOption;

    const std::string program = find_program(cmdline[0]);
    if (program.empty()) throw CommandNotFoundError(std::format("Command \"{}\" not found.", cmdline[0]));

    Popen process{};

    PipePair cin_pair;
    PipePair cout_pair;
    PipePair cerr_pair;

    PROCESS_INFORMATION piProcInfo{};
    STARTUPINFO siStartInfo{};
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    siStartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    siStartInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    // Configure a stdio pipe. is_input: true for stdin (child reads), false for stdout/stderr (child writes).
    // 'close' and 'pipe' share pipe creation; only 'pipe' exposes the parent end to the caller.
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

    if (!bSuccess) throw SpawnError(std::format("CreateProcess failed: {}", LastErrorString()));

    return process;
}
} // namespace subprocess

#endif
