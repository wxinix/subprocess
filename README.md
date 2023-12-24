# subprocess

This C++ subprocess library draws inspiration from the design of Python's subprocess module and has been adapted and extensively refactored with a focus for the Windows platform. 

The codebase, originally forked from https://github.com/benman64/subprocess has undergone significant improvements to ensure compatibility and efficiency on Windows systems. The codebase under this repository is maintained by [@wxinix](https://github.com/wxinix).

As part of the enhancement process, unit tests have been rewritten, transitioning from CxxTest to DocTest for improved testing and validation.

# supports

- Python like style of subprocess, ith a nice syntax enabled by C++20.
- Connect output of process A to input of process B.
- Environment utilities to make it easy to get/set environment variables, as easy as `subprocess::cenv["MY_VAR"] = "value"`.
- subprocess::EnvGuard that will save the environment and reload it when scope block ends, making it easy to have a temporary environment. This is not thread safe as environment variable changes effects process wide.
- Get a copy of environment, so you can modify std::map as needed for use in a thread safe manner of environment and pass it along to subprocesses.
- Cross-platform `find_program`, which has special handling of "python3" on windows making it easy to find python3 executable. It searches the path for python and inspects its version so that `find_program("python3")` is cross-platform.
- Supports connecting process stdin, stdout, stderr to C++ streams making redirection convenient. `stdin` can be connected with std::string too.

# requirements

- C++20
- On Unix-like systems, need to be linked with lib support for threading, filesystem

# Examples

```cpp
#include <subprocess.hpp>
#include <thread>
#include <cstring>

void simple()
{
    using subprocess::CompletedProcess;
    using subprocess::RunBuilder;
    using subprocess::PipeOption;
	
    // Quick echo it, doesn't capture
    subprocess::run({"echo", "hello", "world"});

    // Simplest capture output.
    CompletedProcess process = subprocess::run(
        {"echo", "hello", "world"}, 
        RunBuilder().cout(PipeOption::pipe)
    );

    // Simplest sending data example
    process = subprocess::run({"cat"}, RunBuilder().cin("hello world\n"));

    // Simplest send & capture
    process = subprocess::run(
        {"cat"}, 
        RunBuilder().cin("hello world").cout(PipeOption::pipe)
    );

    std::cout << "captured: " << process.cout << '\n';

    // Capture stderr too. Will throw CalledProcessError if returncode != 0.
    process = subprocess::run(
        {"echo", "hello", "world"}, 
        RunBuilder().cerr(PipeOption::pipe).cout(PipeOption::pipe).raise_on_nonzero(true)
    );

    // There is no cerr so it will be empty
    std::cout << "cerr was: " << process.cerr << "\n";

    // Supported with C++20 syntax, raise_on_nonzero = true will throw exception upon errors
    process = subprocess::run({"echo", "hello", "world"}, {.cout = PipeOption::pipe, .raise_on_nonzero = false});

    std::cout << "captured: " << process.cout << '\n';
}


void popen_examples()
{
    using subprocess::CompletedProcess;
    using subprocess::RunBuilder;
    using subprocess::Popen;
    using subprocess::PipeOption;

    // Capture is enabled by default
    Popen popen = subprocess::RunBuilder({"echo", "hello", "world"}).cout(PipeOption::pipe).popen();
	
    // Initializes everything to 0
    char buf[1024] = {0}; 
    subprocess::pipe_read(popen.cout, buf, 1024);
    std::cout << buf;
	
    // Destructor will call wait() on your behalf
    popen.close();

    // Communicate with data
    popen = subprocess::RunBuilder({"cat"}).cin(PipeOption::pipe).cout(PipeOption::pipe).popen();
	
    // Spawn a new thread for writing
    std::thread write_thread([&]() {
        subprocess::pipe_write(popen.cin, "hello world\n", std::strlen("hello world\n"));
        popen.close_cin();
    });

    // Reset buffer
    std::fill(std::begin(buf), std::end(buf), 0);

    subprocess::pipe_read(popen.cout, buf, 1024);
    std::cout << buf;
    popen.close();
	
    if (write_thread.joinable())
    {
        write_thread.join();
    }
}

int main(int argc, char** argv) 
{
    std::cout << "running basic examples\n";
    simple();
    std::cout << "running popen_examples\n";
    popen_examples();
    return 0;
}
```

# Deviations

- On windows terminating a process sends `CTRL_BREAK_EVENT` instead of hard termination. You can send a `SIGKILL` and it will do a hard termination as expected. Be careful as this may kill your process as it's sent to the process group. See `send_signal` for more details. Also see the test case `SUBCASE("can send SIGNINT")`.
- `cin`, `cout`, `cerr` variable names are used instead of `stdin`, `stdout`, `stderr` as std* are macros and cannot be used as names in C++.