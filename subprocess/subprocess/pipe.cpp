#include "pipe.h"

#include <algorithm>
#include <array>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/ioctl.h>
#include <poll.h>

#include <cerrno>
#endif

#include "utf8_to_utf16.h"

namespace subprocess {

// Toggle a bit flag: sets if `set` is true, clears if false
template<typename T>
constexpr void toggle_flag(T& flags, const T flag, const bool set) {
    if (set)
        flags |= flag;
    else
        flags &= ~flag;
}

PipePair& PipePair::operator=(PipePair&& other) noexcept {
    close();
    input = other.input;
    output = other.output;
    other.disown();
    return *this;
}

void PipePair::close() {
    close_input();
    close_output();
}

void PipePair::close_handle(PipeHandle PipePair::*member) {
    if (this->*member != kBadPipeValue) {
        (void)pipe_close(this->*member);
        this->*member = kBadPipeValue;
    }
}

// =============================================================================
// Windows implementations
// =============================================================================

#ifdef _WIN32

void pipe_set_inheritable(const PipeHandle handle, const bool inheritable) {
    if (handle == kBadPipeValue) throw std::invalid_argument("pipe_set_inheritable: handle is invalid");

    if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, inheritable ? HANDLE_FLAG_INHERIT : 0))
        throw OSError("SetHandleInformation failed");
}

bool pipe_close(const PipeHandle handle) {
    return CloseHandle(handle) != 0;
}

PipePair pipe_create(const bool inheritable) {
    SECURITY_ATTRIBUTES security{.nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = inheritable};

    PipeHandle input;
    PipeHandle output;

    if (!CreatePipe(&input, &output, &security, 0)) throw OSError("could not create pipe");

    return {input, output};
}

ssize_t pipe_read(const PipeHandle handle, void* buffer, const std::size_t size) {
    DWORD bread = 0;
    return ReadFile(handle, buffer, static_cast<DWORD>(size), &bread, nullptr) ? static_cast<ssize_t>(bread) : -1;
}

ssize_t pipe_write(const PipeHandle handle, const void* buffer, const size_t size) {
    DWORD written = 0;
    return WriteFile(handle, buffer, static_cast<DWORD>(size), &written, nullptr) ? static_cast<ssize_t>(written) : -1;
}

bool pipe_set_blocking(const PipeHandle handle, const bool should_block) {
    DWORD state = 0;
    if (!GetNamedPipeHandleStateA(handle, &state, nullptr, nullptr, nullptr, nullptr, 0))
        return false;

    toggle_flag(state, static_cast<DWORD>(PIPE_NOWAIT), !should_block);
    return SetNamedPipeHandleState(handle, &state, nullptr, nullptr) != 0;
}

ssize_t pipe_peek_bytes(const PipeHandle handle) {
    DWORD available = 0;
    return PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) ? static_cast<ssize_t>(available) : -1;
}

int pipe_wait_for_read(const PipeHandle handle, const double seconds) {
    // Save and restore blocking state via RAII
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
    double remaining = seconds;

    while (true) {
        const auto dw_timeout = static_cast<DWORD>(remaining < 0 ? INFINITE : remaining * 1000.0);
        const DWORD result = WaitForSingleObject(handle, dw_timeout);

        if (result == WAIT_OBJECT_0) {
            DWORD available = 0;
            if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) return -1;
            if (available > 0) return 1;
            if (remaining < 0) continue; // infinite timeout, keep waiting
            // Spurious wake — subtract elapsed and retry
            // (conservative: assume we waited the full timeout)
            return 0;
        }

        if (result == WAIT_TIMEOUT) return 0;
        return -1;
    }
}

PipeHandle pipe_file(const std::string_view filename, const std::string_view mode) {
    DWORD access = 0;
    DWORD disposition = 0;

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
    HANDLE hFile =
        CreateFileW(reinterpret_cast<LPCWSTR>(wide.c_str()), access, 0, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);

    return (hFile != INVALID_HANDLE_VALUE) ? hFile : kBadPipeValue;
}

// =============================================================================
// POSIX implementations
// =============================================================================

#else

void pipe_set_inheritable(const PipeHandle handle, const bool inherits) {
    if (handle == kBadPipeValue) throw std::invalid_argument("pipe_set_inheritable: handle is invalid");

    int flags = fcntl(handle, F_GETFD);
    if (flags < 0) details::throw_os_error("fcntl", errno);

    toggle_flag(flags, FD_CLOEXEC, !inherits);

    if (fcntl(handle, F_SETFD, flags) < 0) details::throw_os_error("fcntl", errno);
}

bool pipe_close(const PipeHandle handle) {
    if (handle == kBadPipeValue) return false;
    return ::close(handle) == 0;
}

PipePair pipe_create(const bool inheritable) {
    int fd[2];
    if (::pipe(fd) != 0) {
        details::throw_os_error("pipe", errno);
        return {};
    }

    if (!inheritable) {
        pipe_set_inheritable(fd[0], false);
        pipe_set_inheritable(fd[1], false);
    }

    return {fd[0], fd[1]};
}

ssize_t pipe_read(const PipeHandle handle, void* buffer, const size_t size) {
    return ::read(handle, buffer, size);
}

ssize_t pipe_write(const PipeHandle handle, const void* buffer, const size_t size) {
    return ::write(handle, buffer, size);
}

bool pipe_set_blocking(const PipeHandle handle, const bool should_block) {
    int flags = fcntl(handle, F_GETFL);
    if (flags < 0) return false;

    toggle_flag(flags, O_NONBLOCK, !should_block);
    return fcntl(handle, F_SETFL, flags) == 0;
}

ssize_t pipe_peek_bytes(const PipeHandle handle) {
    int available = 0;
    return (ioctl(handle, FIONREAD, &available) == 0) ? static_cast<ssize_t>(available) : -1;
}

int pipe_wait_for_read(const PipeHandle handle, const double seconds) {
    pollfd pfd{.fd = handle, .events = POLLIN, .revents = 0};
    const int ms = (seconds < 0) ? -1 : static_cast<int>(seconds * 1000.0);
    const int ret = poll(&pfd, 1, ms);

    if (ret > 0) return (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) ? -1 : 1;
    if (ret == 0) return 0;
    return -1;
}

PipeHandle pipe_file(const std::string_view filename, const std::string_view mode) {
    int flags = 0;
    if (mode.contains('r')) flags = O_RDONLY;
    if (mode.contains('w')) flags |= O_WRONLY | O_CREAT | O_TRUNC;
    if (mode.contains('+')) flags |= O_RDWR;

    const int fd = open(std::string{filename}.c_str(), flags, 0666);
    return (fd >= 0) ? fd : kBadPipeValue;
}

#endif

// =============================================================================
// Cross-platform implementations
// =============================================================================

std::string pipe_read_all(const PipeHandle handle) {
    std::string result;
    if (handle == kBadPipeValue) return result;

    std::array<char, kPipeBufferSize> buf{};

    for (ssize_t n = pipe_read(handle, buf.data(), buf.size()); n > 0;
         n = pipe_read(handle, buf.data(), buf.size())) {
        result.append(buf.data(), static_cast<size_t>(n));
    }

    return result;
}

ssize_t pipe_read_some(const PipeHandle handle, void* buffer, const size_t size) {
    if (size == 0) return 0;

    // Block until at least 1 byte arrives
    const ssize_t first = pipe_read(handle, buffer, 1);
    if (first <= 0) return first;

    // Then drain whatever is immediately available
    const ssize_t available = pipe_peek_bytes(handle);
    if (available <= 0) return 1;

    auto* cursor = static_cast<uint8_t*>(buffer) + 1;
    const auto to_read = (std::min)(static_cast<ssize_t>(size - 1), available);
    const ssize_t more = pipe_read(handle, cursor, static_cast<size_t>(to_read));

    return (more < 0) ? more : 1 + more;
}

ssize_t pipe_write_fully(const PipeHandle handle, const void* buffer, const size_t size) {
    auto* cursor = static_cast<const uint8_t*>(buffer);
    ssize_t total = 0;

    while (static_cast<size_t>(total) < size) {
        const ssize_t n = pipe_write(handle, cursor, size - static_cast<size_t>(total));
        if (n < 0) return -(total + 1); // negative encodes how much was written before error
        if (n == 0) break;
        cursor += n;
        total += n;
    }

    return total;
}

void pipe_ignore_and_close(const PipeHandle handle) {
    if (handle == kBadPipeValue) return;

    std::thread([handle]() {
        std::array<uint8_t, kPipeBufferSize> buffer{};
        while (pipe_read(handle, buffer.data(), buffer.size()) >= 0) {}
        (void)pipe_close(handle);
    }).detach();
}

} // namespace subprocess
