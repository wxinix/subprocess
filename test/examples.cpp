#include <cstring>
#include <filesystem>
#include <print>
#include <thread>

#include <subprocess.h>

void simple() {
    using subprocess::CompletedProcess;
    using subprocess::PipeOption;
    using subprocess::RunBuilder;

    // Quick echo - doesn't capture
    subprocess::run({"echo", "hello", "world"});

    // Simplest capture output
    CompletedProcess process = subprocess::run({"echo", "hello", "world"}, RunBuilder().cout(PipeOption::pipe));

    // Simplest send & capture
    process = subprocess::run({"cat"}, RunBuilder().cin("hello world").cout(PipeOption::pipe));
    std::println("Captured: {}", process.cout);

    // Capture stderr too, will throw CalledProcessError if returncode != 0
    process = subprocess::run({"echo", "hello", "world"},
                              RunBuilder().cerr(PipeOption::pipe).cout(PipeOption::pipe).raise_on_nonzero(true));

    std::println("cerr was: {}", process.cerr);

    // C++20 designated initializers
    process = subprocess::run({"echo", "hello", "world"}, {.cout = PipeOption::pipe, .raise_on_nonzero = false});
    std::println("captured: {}", process.cout);
}

void popen_examples() {
    using subprocess::CompletedProcess;
    using subprocess::PipeOption;
    using subprocess::Popen;
    using subprocess::RunBuilder;

    // Simplest example; capture is enabled by default
    Popen popen = RunBuilder({"echo", "hello", "world"}).cout(PipeOption::pipe).popen();

    char buf[1024] = {};
    subprocess::pipe_read(popen.cout, buf, 1024);
    std::print("{}", buf);

    popen.close();

    // Communicate with data
    popen = RunBuilder({"cat"}).cin(PipeOption::pipe).cout(PipeOption::pipe).popen();

    std::thread write_thread([&]() {
        subprocess::pipe_write(popen.cin, "hello world\n", std::strlen("hello world\n"));
        popen.close_cin();
    });

    std::fill(std::begin(buf), std::end(buf), 0);
    subprocess::pipe_read(popen.cout, buf, 1024);
    std::print("{}", buf);
    popen.close();

    if (write_thread.joinable()) write_thread.join();
}

int main(int, char** argv) {
    try {
        std::string envPath = subprocess::cenv["PATH"];
        std::filesystem::path exeDir = std::filesystem::path{argv[0]}.parent_path();
        envPath = envPath + subprocess::kPathDelimiter + exeDir.string();
        subprocess::cenv["PATH"] = envPath;

        std::println("Running basic examples.");
        simple();
        std::println("Running popen_examples.");
        popen_examples();
        return 0;
    } catch (subprocess::SubprocessError& e) {
        std::println(stderr, "{}", e.what());
        return -1;
    }
}
