#include <cstring>
#include <filesystem>
#include <print>
#include <thread>

#include <subproc.hpp>

void simple() {
    using subproc::CompletedProcess;
    using subproc::PipeOption;
    using subproc::RunBuilder;

    // Quick echo - doesn't capture
    (void)subproc::run({"echo", "hello", "world"});

    // Simplest capture output
    CompletedProcess process = subproc::run({"echo", "hello", "world"}, RunBuilder().cout(PipeOption::pipe));

    // Simplest send & capture
    process = subproc::run({"cat"}, RunBuilder().cin("hello world").cout(PipeOption::pipe));
    std::println("Captured: {}", process.cout);

    // Capture stderr too, will throw CalledProcessError if returncode != 0
    process = subproc::run({"echo", "hello", "world"},
                              RunBuilder().cerr(PipeOption::pipe).cout(PipeOption::pipe).raise_on_nonzero(true));

    std::println("cerr was: {}", process.cerr);

    // C++20 designated initializers
    process = subproc::run({"echo", "hello", "world"}, {.cout = PipeOption::pipe, .raise_on_nonzero = false});
    std::println("captured: {}", process.cout);
}

void popen_examples() {
    using subproc::CompletedProcess;
    using subproc::PipeOption;
    using subproc::Popen;
    using subproc::RunBuilder;

    // Simplest example; capture is enabled by default
    Popen popen = RunBuilder({"echo", "hello", "world"}).cout(PipeOption::pipe).popen();

    char buf[1024] = {};
    (void)subproc::pipe_read(popen.cout, buf, 1024);
    std::print("{}", buf);

    popen.close();

    // Communicate with data
    popen = RunBuilder({"cat"}).cin(PipeOption::pipe).cout(PipeOption::pipe).popen();

    std::thread write_thread([&]() {
        (void)subproc::pipe_write(popen.cin, "hello world\n", std::strlen("hello world\n"));
        popen.close_cin();
    });

    std::fill(std::begin(buf), std::end(buf), 0);
    (void)subproc::pipe_read(popen.cout, buf, 1024);
    std::print("{}", buf);
    popen.close();

    if (write_thread.joinable()) write_thread.join();
}

int main(int, char** argv) {
    try {
        std::string envPath = subproc::cenv["PATH"];
        std::filesystem::path exeDir = std::filesystem::path{argv[0]}.parent_path();
        envPath = envPath + subproc::kPathDelimiter + exeDir.string();
        subproc::cenv["PATH"] = envPath;

        std::println("Running basic examples.");
        simple();
        std::println("Running popen_examples.");
        popen_examples();
        return 0;
    } catch (subproc::SubprocError& e) {
        std::println(stderr, "{}", e.what());
        return -1;
    }
}
