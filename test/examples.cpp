#include <cstring>
#include <filesystem>
#include <subprocess.h>
#include <thread>

void simple()
{
    using subprocess::CompletedProcess;
    using subprocess::PipeOption;
    using subprocess::RunBuilder;

    // Quick echo it, doesn't capture
    subprocess::run({"echo", "hello", "world"});

    // Simplest capture output.
    CompletedProcess process = subprocess::run({"echo", "hello", "world"}, RunBuilder().cout(PipeOption::pipe));

    // Simplest sending data example
    // process = subprocess::run({"cat"}, RunBuilder().cin("hello world\n"));

    // Simplest send & capture
    process = subprocess::run({"cat"}, RunBuilder().cin("hello world").cout(PipeOption::pipe));
    std::cout << "Captured: " << process.cout << '\n';

    // Capture stderr too, will throw CalledProcessError if returncode != 0.
    process = subprocess::run({"echo", "hello", "world"},
                              RunBuilder().cerr(PipeOption::pipe).cout(PipeOption::pipe).raise_on_nonzero(true));

    // There is no cerr so it will be empty
    std::cout << "cerr was: " << process.cerr << "\n";

#if __cplusplus >= 202002L
    // Capture output, raise_on_nonzero = true to throw exception
    process = subprocess::run({"echo", "hello", "world"}, {.cout = PipeOption::pipe, .raise_on_nonzero = false});
    std::cout << "captured: " << process.cout << '\n';
#endif
}

void popen_examples()
{
    using subprocess::CompletedProcess;
    using subprocess::PipeOption;
    using subprocess::Popen;
    using subprocess::RunBuilder;

    // Simplest example; capture is enabled by default
    Popen popen = subprocess::RunBuilder({"echo", "hello", "world"}).cout(PipeOption::pipe).popen();

    char buf[1024] = {0}; // initializes everything to 0
    subprocess::pipe_read(popen.cout, buf, 1024);
    std::cout << buf;

    // Destructor will call wait on your behalf.
    popen.close();

    // Communicate with data
    popen = subprocess::RunBuilder({"cat"}).cin(PipeOption::pipe).cout(PipeOption::pipe).popen();

    /*
        If we don't use a thread here, and sending more data than the buffer size
        set in the subprocess cat, pipe_write would block (deadlock) because the
        cat.exe subprocess itself was deadlocked trying to write the data it received.
        See the source code of cat_child.cpp for details.
    */
    std::thread write_thread(
        [&]()
        {
            subprocess::pipe_write(popen.cin, "hello world\n", std::strlen("hello world\n"));
            // No more data to send.If we don't close, the subprocess cat.exe might deadlock.
            // See the source code of cat_child.cpp for details.
            popen.close_cin();
        });

    // Reset buf
    std::fill(std::begin(buf), std::end(buf), 0);

    subprocess::pipe_read(popen.cout, buf, 1024);
    std::cout << buf;
    popen.close();

    if (write_thread.joinable())
    {
        write_thread.join();
    }
}

int main(int, char** argv)
{
    try
    {
        std::string envPath = subprocess::cenv["PATH"];
        std::filesystem::path exeDir = std::filesystem::path{argv[0]}.parent_path();
        envPath = envPath + subprocess::kPathDelimiter + exeDir.string();
        subprocess::cenv["PATH"] = envPath;

        std::cout << "Running basic examples.\n";
        simple();
        std::cout << "Running popen_examples.\n";
        popen_examples();
        return 0;
    }
    catch (subprocess::SubprocessError& e)
    {
        std::cout << e.what() << std::endl;
        return -1;
    }
}