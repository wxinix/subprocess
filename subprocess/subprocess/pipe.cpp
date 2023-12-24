#include "pipe.h"

#include <thread>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#endif

namespace subprocess
{

PipePair& PipePair::operator=(PipePair&& other) noexcept
{
    close();
    const_cast<PipeHandle&>(input) = other.input;
    const_cast<PipeHandle&>(output) = other.output;
    other.disown();
    return *this;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantConditionsOC"
#pragma ide diagnostic ignored "UnreachableCode"

void PipePair::close()
{
    if (input != kBadPipeValue)
    {
        (void)pipe_close(input);
    }

    if (output != kBadPipeValue)
    {
        (void)pipe_close(output);
    }

    disown();
}

void PipePair::close_input()
{
    if (input != kBadPipeValue)
    {
        (void)pipe_close(input);
        const_cast<PipeHandle&>(input) = kBadPipeValue;
    }
}

void PipePair::close_output()
{
    if (output != kBadPipeValue)
    {
        (void)pipe_close(output);
        const_cast<PipeHandle&>(output) = kBadPipeValue;
    }
}
#pragma clang diagnostic pop

#ifdef _WIN32
void pipe_set_inheritable(subprocess::PipeHandle handle, bool inheritable)
{
    if (handle == kBadPipeValue)
    {
        throw std::invalid_argument("pipe_set_inheritable: handle is invalid");
    }

    bool success = 0 != SetHandleInformation(handle,                                  //
                                             static_cast<DWORD>(HANDLE_FLAG_INHERIT), //
                                             static_cast<DWORD>(inheritable ? HANDLE_FLAG_INHERIT : 0));
    if (!success)
    {
        throw OSError("SetHandleInformation failed");
    }
}

bool pipe_close(PipeHandle handle)
{
    return 0 != CloseHandle(handle);
}

PipePair pipe_create(bool inheritable)
{
    SECURITY_ATTRIBUTES security = {0U};
    security.nLength = static_cast<DWORD>(sizeof(security));
    security.bInheritHandle = inheritable;

    PipeHandle input;
    PipeHandle output;

    bool result = CreatePipe(&input, &output, &security, 0U);
    if (!result)
    {
        input = kBadPipeValue;
        output = kBadPipeValue;
        throw OSError("could not create pipe");
    }

    return {input, output};
}

ssize_t pipe_read(PipeHandle handle, void* buffer, std::size_t size)
{
    DWORD bread = 0U;
    bool result = ReadFile(handle, buffer, static_cast<DWORD>(size), &bread, nullptr);
    return result ? static_cast<ssize_t>(bread) : -1;
}

ssize_t pipe_write(PipeHandle handle, const void* buffer, size_t size)
{
    DWORD written = 0U;
    bool result = WriteFile(handle, buffer, static_cast<DWORD>(size), &written, nullptr);
    return result ? static_cast<ssize_t>(written) : -1;
}

#else
void pipe_set_inheritable(PipeHandle handle, bool inherits)
{
    if (handle == kBadPipeValue)
        throw std::invalid_argument("pipe_set_inheritable: handle is invalid");

    int flags = fcntl(handle, F_GETFD);

    if (flags < 0)
        throw_os_error("fcntl", errno);

    if (inherits)
        flags &= ~FD_CLOEXEC;
    else
        flags |= FD_CLOEXEC;

    int result = fcntl(handle, F_SETFD, flags);
    if (result < -1)
        throw_os_error("fcntl", errno);
}
bool pipe_close(PipeHandle handle)
{
    if (handle == kBadPipeValue)
        return false;

    return ::close(handle) == 0;
}

PipePair pipe_create(bool inheritable)
{
    int fd[2];
    bool success = !::pipe(fd);
    if (!success)
    {
        throw_os_error("pipe", errno);
        return {};
    }

    if (!inheritable)
    {
        pipe_set_inheritable(fd[0], false);
        pipe_set_inheritable(fd[1], false);
    }

    return {fd[0], fd[1]};
}

ssize_t pipe_read(PipeHandle handle, void* buffer, size_t size)
{
    return ::read(handle, buffer, size);
}

ssize_t pipe_write(PipeHandle handle, const void* buffer, size_t size)
{
    return ::write(handle, buffer, size);
}
#endif

std::string pipe_read_all(PipeHandle handle)
{
    std::string result{};

    if (handle != kBadPipeValue)
    {
        constexpr size_t buf_size = 2048U;
        uint8_t buf[buf_size];
        ssize_t transfered;

        do
        {
            if (transfered = pipe_read(handle, static_cast<void*>(&buf[0]), buf_size); transfered > 0)
            {
                (void)result.insert(result.end(), &buf[0], &buf[transfered]);
            }
        } while (transfered > 0);
    }

    return result;
}

void pipe_ignore_and_close(PipeHandle handle)
{
    if (handle != kBadPipeValue)
    {
        std::thread thread(
            [handle]()
            {
                std::vector<uint8_t> buffer(1024U);
                while (pipe_read(handle, &buffer[0U], buffer.size()) >= 0)
                {
                }
                (void)pipe_close(handle);
            });

        thread.detach();
    }
}

} // namespace subprocess