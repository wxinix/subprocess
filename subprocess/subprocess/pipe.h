#pragma warning(disable : 4068)
#pragma clang diagnostic push
#pragma ide diagnostic ignored "clion-misra-cpp2008-11-0-1"

#pragma once

#include "basic_types.hpp"

namespace subprocess
{

/**
 * Represents a pair of pipes for input and output. The design follows a C-like
 * API due to the complexity involved in supporting various C++ use-cases. Users
 * are encouraged to use RAII classes specific to their needs.
 */
struct PipePair
{
    PipePair() = default;

    PipePair(PipeHandle input, PipeHandle output) : input(input), output(output)
    {
    }

    ~PipePair()
    {
        close();
    }

    // No copy, move only
    PipePair(const PipePair&) = delete;
    PipePair& operator=(const PipePair&) = delete;

    PipePair(PipePair&& other) noexcept
    {
        *this = std::move(other);
    }

    PipePair& operator=(PipePair&& other) noexcept;

    /**
     * @brief Represents a pair of pipe handles for input and output.
     *
     * The handles are initially set to kBadPipeValue to indicate that they
     * are not associated with valid pipes. These handles are marked as const to
     * communicate that their values should not be modified directly in the code.
     * However, the class provides methods like disown, close*, and move semantics
     * that allow users to modify these values intentionally.
     *
     * @note Modifying the handles directly outside of these designated methods
     *       may lead to unexpected behavior.
     */
    const PipeHandle input = kBadPipeValue;
    const PipeHandle output = kBadPipeValue;

    /** Stop owning the pipes */
    void disown()
    {
        // const_cast here is to enable the modification of input and output,
        // even though these members are marked as const.
        const_cast<PipeHandle&>(input) = kBadPipeValue;
        const_cast<PipeHandle&>(output) = kBadPipeValue;
    }

    void close();
    void close_input();
    void close_output();

    explicit operator bool() const noexcept
    {
        return input != output;
    }
};

/**
 * Closes a pipe handle.
 *
 * @param handle The handle to close.
 * @return true on success, false on failure.
 */
bool pipe_close(PipeHandle handle);

/**
 * Creates a pair of pipes for input/output.
 *
 * @param inheritable If true, subprocesses will inherit the pipe.
 * @throw OSError if the system call fails.
 * @return Pipe pair. If failure, returned pipes will have values of kBadPipeValue.
 */
PipePair pipe_create(bool inheritable = true);

/**
 * Sets the pipe to be inheritable or not for subprocess.
 *
 * @param handle The pipe handle.
 * @param inheritable If true, the handle will be inherited in subprocess.
 * @throw OSError if the system call fails.
 */
void pipe_set_inheritable(PipeHandle handle, bool inheritable);

/**
 * Reads from the pipe until no more data is available.
 *
 * @param handle The pipe handle.
 * @param buffer The buffer to read into.
 * @param size The size of the buffer.
 * @return -1 on error. If 0, it could be the end or perhaps wait for more data.
 */
ssize_t pipe_read(PipeHandle handle, void* buffer, size_t size);

/**
 * Writes to the pipe.
 *
 * @param handle The pipe handle.
 * @param buffer The buffer containing data to write.
 * @param size The size of the buffer.
 * @return -1 on error. If 0, it could be full or perhaps wait for more data.
 */
ssize_t pipe_write(PipeHandle handle, const void* buffer, size_t size);

/**
 * Spawns a thread to read from the pipe. When no more data is available,
 * the pipe will be closed.
 *
 * @param handle The pipe handle.
 */
void pipe_ignore_and_close(PipeHandle handle);

/**
 * Reads contents of handle until no more data is available.
 * If the pipe is non-blocking, this will end prematurely.
 *
 * @param handle The pipe handle.
 * @return All data read from the pipe as a string object.
 *         This works fine with binary data.
 */
std::string pipe_read_all(PipeHandle handle);

} // namespace subprocess

#pragma clang diagnostic pop