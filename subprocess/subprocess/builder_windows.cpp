#ifdef _WIN32

#include "builder.h"

#include <strsafe.h>
#include <windows.h>

#include "environ.h"
#include "shellutils.h"

namespace subprocess
{

STARTUPINFO g_startupInfo;
auto g_startupInfoInit = false;

void init_startup_info()
{
    if (g_startupInfoInit)
    {
        GetStartupInfo(&g_startupInfo);
    }
}

bool disable_inherit(subprocess::PipeHandle handle)
{
    return 0 != SetHandleInformation(handle, static_cast<DWORD>(HANDLE_FLAG_INHERIT), 0U);
}

Popen ProcessBuilder::run_command(const CommandLine& cmdline)
{
    std::string program = find_program(cmdline[0U]);
    if (program.empty())
    {
        throw CommandNotFoundError(std::format("Command \"{}\" not found.", cmdline[0U]));
    }
    init_startup_info();

    Popen process{};

    PipePair cin_pair;
    PipePair cout_pair;
    PipePair cerr_pair;
    PipePair closed_pair;

    SECURITY_ATTRIBUTES saAttr = {0U};

    // Set the bInheritHandle flag so pipe handles are inherited.
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); // NOLINT
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    PROCESS_INFORMATION piProcInfo = {nullptr};
    STARTUPINFO siStartInfo = {0U};
    BOOL bSuccess = FALSE;

    siStartInfo.cb = sizeof(STARTUPINFO);                     // NOLINT
    siStartInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);   // NOLINT
    siStartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); // NOLINT
    siStartInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);   // NOLINT
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    if (cin_option == PipeOption::close)
    {
        cin_pair = pipe_create();
        siStartInfo.hStdInput = cin_pair.input;
        (void)disable_inherit(cin_pair.output);
    }
    else if (cin_option == PipeOption::specific)
    {
        pipe_set_inheritable(cin_pipe, true);
        siStartInfo.hStdInput = cin_pipe;
    }
    else if (cin_option == PipeOption::pipe)
    {
        cin_pair = pipe_create();
        siStartInfo.hStdInput = cin_pair.input;
        process.cin = cin_pair.output;
        (void)disable_inherit(cin_pair.output);
    }
    else
    {
    }

    if (cout_option == PipeOption::close)
    {
        cout_pair = pipe_create();
        siStartInfo.hStdOutput = cout_pair.output;
        (void)disable_inherit(cout_pair.input);
    }
    else if (cout_option == PipeOption::pipe)
    {
        cout_pair = pipe_create();
        siStartInfo.hStdOutput = cout_pair.output;
        process.cout = cout_pair.input;
        (void)disable_inherit(cout_pair.input);
    }
    else if (cout_option == PipeOption::specific)
    {
        pipe_set_inheritable(cout_pipe, true);
        siStartInfo.hStdOutput = cout_pipe;
    }
    else // (cout_option == PipeOption::cerr)
    {
    }

    if (cerr_option == PipeOption::close)
    {
        cerr_pair = pipe_create();
        siStartInfo.hStdError = cerr_pair.output;
        (void)disable_inherit(cerr_pair.input);
    }
    else if (cerr_option == PipeOption::pipe)
    {
        cerr_pair = pipe_create();
        siStartInfo.hStdError = cerr_pair.output;
        process.cerr = cerr_pair.input;
        (void)disable_inherit(cerr_pair.input);
    }
    else if (cerr_option == PipeOption::cout)
    {
        siStartInfo.hStdError = siStartInfo.hStdOutput;
    }
    else if (cerr_option == PipeOption::specific)
    {
        pipe_set_inheritable(cerr_pipe, true);
        siStartInfo.hStdError = cerr_pipe;
    }
    else
    {
    }

    // I don't know why someone would want to do this. But for completeness
    if (cout_option == PipeOption::cerr)
    {
        siStartInfo.hStdOutput = siStartInfo.hStdError;
    }
    const char* l_cwd = this->cwd.empty() ? nullptr : this->cwd.c_str();
    std::string args = windows_args(cmdline);

    void* l_env = nullptr;
    std::u16string envblock;
    if (!this->env.empty())
    {
        /*  if you use ansi there is a 37K size limit. So we use unicode
            which is almost utf16.
            TODO: fix by using unicode 16bit chars.
            This won't work as expected if somewhere there is a multibyte
            utf-16 char (4-bytes total).
        */
        envblock = create_env_block(this->env);
        l_env = static_cast<void*>(envblock.data());
    }

    DWORD process_flags = CREATE_UNICODE_ENVIRONMENT; // NOLINT
    if (this->new_process_group)
    {
        process_flags |= CREATE_NEW_PROCESS_GROUP; // NOLINT
    }

    // Create the child process.
    bSuccess = CreateProcess(program.c_str(),
                             args.data(),   // command line
                             nullptr,       // process security attributes
                             nullptr,       // primary thread security attributes
                             TRUE,          // handles are inherited
                             process_flags, // creation flags
                             l_env,         // environment
                             l_cwd,         // use parent's current directory
                             &siStartInfo,  // STARTUPINFO pointer
                             &piProcInfo);  // receives PROCESS_INFORMATION

    process.process_info = piProcInfo;
    process.pid = piProcInfo.dwProcessId;

    if (cin_pair)
    {
        cin_pair.close_input();
    }

    if (cout_pair)
    {
        cout_pair.close_output();
    }

    if (cerr_pair)
    {
        cerr_pair.close_output();
    }

    if (cin_option == PipeOption::close)
    {
        cin_pair.close();
    }

    if (cout_option == PipeOption::close)
    {
        cout_pair.close();
    }

    if (cerr_option == PipeOption::close)
    {
        cerr_pair.close();
    }

    cin_pair.disown();
    cout_pair.disown();
    cerr_pair.disown();

    process.args = cmdline;
    if (0 == bSuccess)
    {
        auto msg = std::format("CreateProcess failed: {}", LastErrorString());
        throw SpawnError(msg);
    }
    else
    {
        return process;
    }
}

} // namespace subprocess

#endif