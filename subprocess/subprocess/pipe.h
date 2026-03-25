#pragma once

#include <string_view>

#include "basic_types.hpp"

namespace subprocess {

/**
 * Represents a pair of pipes for input and output with RAII ownership.
 */
struct PipePair {
    PipePair() = default;
    PipePair(const PipeHandle input, const PipeHandle output) : input(input), output(output) {}

    ~PipePair() { close(); }

    // No copy, move only
    PipePair(const PipePair&) = delete;
    PipePair& operator=(const PipePair&) = delete;

    PipePair(PipePair&& other) noexcept { *this = std::move(other); }
    PipePair& operator=(PipePair&& other) noexcept;

    PipeHandle input = kBadPipeValue;
    PipeHandle output = kBadPipeValue;

    void disown() {
        input = kBadPipeValue;
        output = kBadPipeValue;
    }

    void close();
    void close_input() { close_handle(&PipePair::input); }
    void close_output() { close_handle(&PipePair::output); }

    explicit operator bool() const noexcept { return input != kBadPipeValue || output != kBadPipeValue; }

private:
    void close_handle(PipeHandle PipePair::*member);
};

// Core pipe operations
bool pipe_close(PipeHandle handle);
[[nodiscard]] PipePair pipe_create(bool inheritable = true);
void pipe_set_inheritable(PipeHandle handle, bool inheritable);

/** @brief Closes a pipe handle and resets it to kBadPipeValue. No-op if already invalid. */
inline void close_and_reset(PipeHandle& h) {
    if (h != kBadPipeValue) {
        (void)pipe_close(h);
        h = kBadPipeValue;
    }
}

// Blocking mode control
bool pipe_set_blocking(PipeHandle handle, bool should_block);

// Read operations
[[nodiscard]] ssize_t pipe_read(PipeHandle handle, void* buffer, size_t size);
[[nodiscard]] std::string pipe_read_all(PipeHandle handle);

/** @brief Peeks at the number of bytes available for reading without consuming them.
 *  @return Number of available bytes, or -1 on error. */
[[nodiscard]] ssize_t pipe_peek_bytes(PipeHandle handle);

/** @brief Reads at least 1 byte, then reads whatever else is available without blocking.
 *  Blocks until the first byte arrives, then drains the available buffer.
 *  @return Total bytes read, or <= 0 on error/EOF. */
[[nodiscard]] ssize_t pipe_read_some(PipeHandle handle, void* buffer, size_t size);

/** @brief Waits until data is available for reading, or timeout expires.
 *  @param handle
 *  @param seconds Timeout in seconds (-1 for infinite).
 *  @return 1 if data available, 0 on timeout, -1 on error/closed. */
[[nodiscard]] int pipe_wait_for_read(PipeHandle handle, double seconds);

// Write operations
ssize_t pipe_write(PipeHandle handle, const void* buffer, size_t size);

/** @brief Writes the entire buffer, retrying partial writes.
 *  @return Total bytes written, or negative on error (-(bytes_written)-1). */
ssize_t pipe_write_fully(PipeHandle handle, const void* buffer, size_t size);

// File as pipe handle
/** @brief Opens a file and returns a pipe-compatible handle.
 *  @param filename Path to the file (UTF-8).
 *  @param mode "r" for read, "w" for write, "r+" for read/write. */
[[nodiscard]] PipeHandle pipe_file(std::string_view filename, std::string_view mode);

// Utilities
void pipe_ignore_and_close(PipeHandle handle);

} // namespace subprocess
