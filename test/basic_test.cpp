#pragma warning(disable : 4068)
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedMacroInspection"

// clang-format off
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
// clang-format on

#include <filesystem>
#include <subprocess.h>
#include <thread>

#ifdef _WIN32
#define EOL "\r\n"
#else
#define EOL "\n"
#endif

using namespace subprocess;

TEST_SUITE_BEGIN("TEST_SUITE subprocess basic tests");

bool is_equal(const CommandLine& a, const CommandLine& b)
{
    if (a.size() != b.size())
        return false;

    for (auto i = 0; i < a.size(); ++i)
    {
        if (a[i] != b[i])
            return false;
    }

    return true;
}

std::string dirname(const std::string& path)
{
    std::filesystem::path fsPath{path};
    return fsPath.parent_path().string();
}

std::string g_exe_dir{};

void prepend_this_to_path()
{
    using subprocess::abspath;
    std::string path = subprocess::cenv["PATH"];
    path = g_exe_dir + subprocess::kPathDelimiter + path;
    subprocess::cenv["PATH"] = path;
}

TEST_CASE("TEST_CASE - utilities")
{

    SUBCASE("can get system path")
    {
        const std::string path = subprocess::cenv["PATH"];
        CHECK(!path.empty());
    }

    SUBCASE("can convert utf16 to utf8")
    {
        const char8_t* utf8Literal = u8"Hello,World!\u4F60";
        // Assigning to std::string
        std::string utf8Str(reinterpret_cast<const char*>(utf8Literal));

        auto utf16Str = subprocess::utf8_to_utf16(utf8Str);
        CHECK_EQ(utf16Str.size(), 13);

        auto utf8StrNew = subprocess::utf16_to_utf8(utf16Str);
        CHECK_EQ(utf8Str, utf8StrNew);
    }

    SUBCASE("will have RAII for env guard")
    {
        std::string path = cenv["PATH"];
        std::string world = subprocess::cenv["HELLO"];
        CHECK_EQ(world, "");

        INFO("entering EnvGuard scope");
        {
            subprocess::EnvGuard guard;
            INFO("setting HELLO=world");
            subprocess::cenv["HELLO"] = "world";
            world = cenv["HELLO"];
            CHECK_EQ(world, "world");
            INFO("exiting EnvGuard scope");
        }

        world = cenv["HELLO"];
        CHECK_EQ(world, "");
        std::string new_path = cenv["PATH"];
        CHECK_EQ(path, new_path);
    }

    SUBCASE("can find a specified program")
    {
        std::string path = subprocess::find_program("echo");
        CHECK(!path.empty());
    }

    SUBCASE("can sleep")
    {
        subprocess::StopWatch sw{};
        subprocess::sleep_seconds(1.0f);
        auto delta = sw.seconds() - 1.0f;
        CHECK(abs(delta) <= 0.1f);
    }
}

TEST_CASE("TEST_CASE - popen")
{
    SUBCASE("can poll a subprocess")
    {
        subprocess::EnvGuard guard;
        prepend_this_to_path();
        auto popen = RunBuilder({"sleep", "3"}).popen();
        subprocess::StopWatch timer;

        int count = 0;
        while (!popen.poll())
            ++count;

        CHECK(count > 100);
        popen.close(); // The destructor will also call this.

        double timeout = timer.seconds();
        CHECK(abs(timeout - 3.0f) <= 0.1);
    }

    SUBCASE("can run timeout")
    {
        subprocess::EnvGuard guard;
        prepend_this_to_path();
        // Using run(),  if timeout, will send a CTRL+BREAK. More graceful than
        // a simple forceful kill
        CHECK_THROWS_AS(subprocess::run({"sleep", "3"}, {.new_process_group = true, .timeout = 1}),
                        subprocess::TimeoutExpired);
    }

    SUBCASE("can wait timeout")
    {
        subprocess::EnvGuard guard;
        prepend_this_to_path();
        auto popen = RunBuilder({"sleep", "10"}).new_process_group(true).popen();
        CHECK_THROWS_AS(popen.wait(3), subprocess::TimeoutExpired);
        (void)popen.terminate();
        (void)popen.close();
    }

    SUBCASE("can kill")
    {
        subprocess::EnvGuard guard;
        prepend_this_to_path();

        auto popen = RunBuilder({"sleep", "10"}).popen();
        subprocess::StopWatch timer;

        std::thread{[&]
                    {
                        subprocess::sleep_seconds(3);
                        (void)popen.kill();
                    }}
            .detach();

        popen.close();
        double timeout = timer.seconds();
        CHECK(abs(timeout - 3.0f) <= 0.1);
    }

    SUBCASE("can terminate")
    {
        subprocess::EnvGuard guard;
        prepend_this_to_path();

        auto popen = RunBuilder({"sleep", "10"}).new_process_group(true).popen();
        subprocess::StopWatch timer;
        std::thread{[&]
                    {
                        subprocess::sleep_seconds(3);
                        (void)popen.terminate();
                    }}
            .detach();

        popen.close();

        double timeout = timer.seconds();
        CHECK(abs(timeout - 3.0f) <= 0.1);
    }

    SUBCASE("can send SIGNINT")
    {
        subprocess::EnvGuard guard;
        prepend_this_to_path();

        // must false for process group
        // See https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent?redirectedfrom=MSDN
        auto popen = RunBuilder({"sleep", "10"}).new_process_group(false).popen();
        subprocess::StopWatch timer;

        std::thread{[&]
                    {
                        subprocess::sleep_seconds(3);
                        (void)popen.send_signal(subprocess::SigNum::PSIGINT);
                    }}
            .detach();

        popen.close();
        double timeout = timer.seconds();
        // https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent?redirectedfrom=MSDN
        // On Windows, CTRL+C won't work. Must use an intermediate program to
        // manage a child process. The intermediate program sends CTRL+C to kill
        // itself and the child processes. This is not implemented by this library.
        // For testing purpose, see Line 421-444, we use the trick, but the CTRL+C
        // will be received by both the parent (i.e., basic_test.exe), and the child
        // process, so at a parent process, we just need to filter it out.
        CHECK(abs(timeout - 3.0f) <= 0.1);
    }

    SUBCASE("can pipe between two subprocess")
    {
        subprocess::EnvGuard guard;
        prepend_this_to_path();

        subprocess::PipePair pp = subprocess::pipe_create(false);
        CHECK(!!pp);

        subprocess::Popen c = RunBuilder({"cat"}).cout(PipeOption::pipe).cin(pp.input).popen();
        subprocess::Popen e = RunBuilder({"echo", "hello", "world"}).cout(pp.output).popen();
        pp.close();
        CompletedProcess p = subprocess::run(c);
        e.close();
        c.close();
        CHECK_EQ(p.cout, "hello world" EOL);
    }
}

TEST_CASE("TEST_CASE - subprocess::run")
{
    SUBCASE("can redirect subprocess output to CompletedProcess cout")
    {
        auto cp = subprocess::run({"echo", "hello", "world"},         //
                                  RunBuilder().cout(PipeOption::pipe) //
        );

        CHECK_EQ(cp.cout, "hello world" EOL);
        CHECK(cp.cerr.empty());
        CHECK_EQ(cp.returncode, 0);
        CommandLine args = {"echo", "hello", "world"};
        CHECK(is_equal(args, cp.args));
    }

    SUBCASE("can redirect subprocess output to CompletedProcess cerr")
    {
        auto cp = subprocess::run({"echo", "hello", "world"},                                //
                                  RunBuilder().cout(PipeOption::cerr).cerr(PipeOption::pipe) //
        );

        CHECK_EQ(cp.cerr, "hello world" EOL);
        CHECK(cp.cout.empty());
        CHECK_EQ(cp.returncode, 0);
        CommandLine args = {"echo", "hello", "world"};
        CHECK(is_equal(args, cp.args));
    }

    SUBCASE("will throw on not found")
    {
        CHECK_THROWS(subprocess::run({"yay-322"}));
    }

    SUBCASE("can use c++20 syntax sugar")
    {
        std::string message = "__cplusplus = " + std::to_string(__cplusplus);
        INFO(message);

#if __cplusplus >= 202002L
        INFO("using C++20");
        auto cp = subprocess::run({"echo", "hello", "world"}, {.cout = PipeOption::pipe});
        CHECK_EQ(cp.cout, "hello world" EOL);
        CHECK(cp.cerr.empty());
        CHECK_EQ(cp.returncode, 0);
        CommandLine args = {"echo", "hello", "world"};
        CHECK(is_equal(cp.args, args));

        cp = subprocess::run({"echo", "hello", "world"}, {.cout = PipeOption::cerr, .cerr = PipeOption::pipe});
        CHECK_EQ(cp.cerr, "hello world" EOL);
        CHECK(cp.cout.empty());
        CHECK_EQ(cp.returncode, 0);
        CHECK_EQ(cp.args, args);
#else
        CHECK(false);
#endif
    }
}

TEST_CASE("TEST_CASE - subprocess::RunBuilder")
{
    SUBCASE("can redirect subprocess output to CompletedProcess cout")
    {
        auto cp = subprocess::RunBuilder({"echo", "hello", "world"}).cout(PipeOption::pipe).run();
        CHECK_EQ(cp.cout, "hello world" EOL);
        CHECK(cp.cerr.empty());
        CHECK_EQ(cp.returncode, 0);
        CommandLine args = {"echo", "hello", "world"};
        CHECK(is_equal(args, cp.args));
    }

    SUBCASE("can redirect subprocess output to CompletedProcess cerr")
    {
        auto cp = subprocess::RunBuilder({"echo", "hello", "world"}) //
                      .cout(PipeOption::cerr)                        //
                      .cerr(PipeOption::pipe)                        //
                      .run();

        CHECK_EQ(cp.cerr, "hello world" EOL);
        CHECK(cp.cout.empty());
        CHECK_EQ(cp.returncode, 0);
        CommandLine args = {"echo", "hello", "world"};
        CHECK(is_equal(args, cp.args));
    }

    SUBCASE("can update env during runtime")
    {
        subprocess::EnvGuard guard;
        prepend_this_to_path();

        subprocess::EnvMap env = subprocess::current_env_copy();
        CHECK(subprocess::cenv["HELLO"].to_string().empty());
        env["HELLO"] = "world";
        CHECK(subprocess::cenv["HELLO"].to_string().empty());

        auto cp = subprocess::RunBuilder({"printenv", "HELLO"}) //
                      .cout(PipeOption::pipe)                   //
                      .env(env)                                 //
                      .run();

        CHECK_EQ(cp.cout, "world" EOL);
    }

    SUBCASE("can redirect cerr to cout")
    {
        subprocess::EnvGuard guard;
        prepend_this_to_path();

        std::string path = subprocess::cenv["PATH"];
        subprocess::cenv["USE_CERR"] = "1";
        subprocess::find_program_clear_cache();
        std::string echo_path = subprocess::find_program("echo");

        auto cp = RunBuilder({"echo", "hello", "world"})
                      .cout(subprocess::PipeOption::pipe)
                      .cerr(subprocess::PipeOption::pipe)
                      .env(subprocess::current_env_copy())
                      .run();

        CHECK_EQ(cp.cout, "");
        CHECK_EQ(cp.cerr, "hello world" EOL);

        CommandLine args = {"echo", "hello", "world"};
        CHECK(is_equal(cp.args, args));

        cp = RunBuilder(args).cerr(subprocess::PipeOption::cout).cout(PipeOption::pipe).run();

        CHECK_EQ(cp.cout, "hello world" EOL);
        CHECK_EQ(cp.cerr, "");
        CHECK(is_equal(cp.args, args));
    }

    SUBCASE("can redirect cout to cerr")
    {
        subprocess::EnvGuard guard;
        prepend_this_to_path();

        auto cp = RunBuilder({"echo", "hello", "world"})  //
                      .cerr(subprocess::PipeOption::pipe) //
                      .cout(PipeOption::cerr)             //
                      .run();

        CHECK_EQ(cp.cout, "");
        CHECK_EQ(cp.cerr, "hello world" EOL);
    }
}

TEST_SUITE_END;

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

BOOL CtrlHandler(DWORD fdwCtrlType)
{
    if (fdwCtrlType == CTRL_C_EVENT)
    {
        std::cout << "basic_test: Ctrl+C received. Ignoring it." << std::endl;
        return TRUE;
    }

    // Return FALSE for unhandled signals
    return FALSE;
}

#else
#include <csignal>

// Global flag to indicate whether Ctrl+C was pressed
volatile std::sig_atomic_t g_signal_received = false;

void signal_handler(int signal)
{
    if (signal == SIGINT)
    {
        std::cout << "Ctrl+C received. Cleaning up and exiting." << std::endl;
        // Perform cleanup or other actions if needed
        g_signal_received.test_and_set();
    }
}

#endif

int main(int argc, char** argv)
{
#if defined(_WIN32) || defined(_WIN64)
    // Set the custom console control handler for Ctrl+C on Windows
    if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE))
#else
    // Set the custom signal handler for Ctrl+C on Unix-like systems
    if (std::signal(SIGINT, signal_handler) != SIG_ERR)
#endif
    {
        std::string path = subprocess::cenv["PATH"];
        g_exe_dir = dirname(abspath(argv[0]));
        path = path + subprocess::kPathDelimiter + g_exe_dir;
        subprocess::cenv["PATH"] = path;
        return doctest::Context(argc, argv).run();
    }
    else
    {
        return 1;
    }
}

#pragma clang diagnostic pop