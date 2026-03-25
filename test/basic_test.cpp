// clang-format off
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
// clang-format on

#include <filesystem>
#include <fstream>
#include <print>
#include <thread>

#include <subproc.hpp>

#ifdef _WIN32
#define EOL "\r\n"
#else
#define EOL "\n"
#endif

using namespace subproc;

TEST_SUITE_BEGIN("TEST_SUITE subprocess basic tests");

std::string g_exe_dir{};

void prepend_this_to_path() {
    std::string path = subproc::cenv["PATH"];
    path = g_exe_dir + subproc::kPathDelimiter + path;
    subproc::cenv["PATH"] = path;
}

// ============================================================================
// Utility tests
// ============================================================================

TEST_CASE("TEST_CASE - utilities") {
    SUBCASE("can get system path") {
        const std::string path = subproc::cenv["PATH"];
        CHECK(!path.empty());
    }

    SUBCASE("can convert utf16 to utf8 roundtrip") {
        const char8_t* utf8Literal = u8"Hello,World!\u4F60";
        std::string utf8Str(reinterpret_cast<const char*>(utf8Literal));

        auto utf16Str = subproc::utf8_to_utf16(utf8Str);
        CHECK_EQ(utf16Str.size(), 13);

        auto utf8StrNew = subproc::utf16_to_utf8(utf16Str);
        CHECK_EQ(utf8Str, utf8StrNew);
    }

    SUBCASE("utf8/utf16 roundtrip with empty string") {
        auto utf16 = subproc::utf8_to_utf16("");
        CHECK(utf16.empty());

        auto utf8 = subproc::utf16_to_utf8(std::u16string{});
        CHECK(utf8.empty());
    }

    SUBCASE("utf8/utf16 roundtrip with ASCII") {
        const std::string ascii = "Hello, World!";
        auto utf16 = subproc::utf8_to_utf16(ascii);
        auto back = subproc::utf16_to_utf8(utf16);
        CHECK_EQ(ascii, back);
    }

    SUBCASE("will have RAII for env guard") {
        const std::string path = cenv["PATH"];
        std::string world = subproc::cenv["HELLO"];
        CHECK_EQ(world, "");

        {
            subproc::EnvGuard guard;
            subproc::cenv["HELLO"] = "world";
            world = cenv["HELLO"];
            CHECK_EQ(world, "world");
        }

        world = cenv["HELLO"];
        CHECK_EQ(world, "");
        const std::string new_path = cenv["PATH"];
        CHECK_EQ(path, new_path);
    }

    SUBCASE("env guard restores modified variables") {
        const std::string original = subproc::cenv["PATH"];
        {
            subproc::EnvGuard guard;
            subproc::cenv["PATH"] = "modified_path";
            CHECK_EQ(std::string(subproc::cenv["PATH"]), "modified_path");
        }
        CHECK_EQ(std::string(subproc::cenv["PATH"]), original);
    }

    SUBCASE("env guard restores deleted variables") {
        subproc::EnvGuard guard;
        subproc::cenv["TEST_ENV_GUARD_DEL"] = "exists";
        CHECK_EQ(std::string(subproc::cenv["TEST_ENV_GUARD_DEL"]), "exists");
        subproc::cenv["TEST_ENV_GUARD_DEL"] = nullptr;
        CHECK_EQ(std::string(subproc::cenv["TEST_ENV_GUARD_DEL"]), "");
    }

    SUBCASE("can find a specified program") {
        const std::string path = subproc::find_program("echo");
        CHECK(!path.empty());
    }

    SUBCASE("find_program returns empty for nonexistent program") {
        const std::string path = subproc::find_program("this_program_does_not_exist_xyz_123");
        CHECK(path.empty());
    }

    SUBCASE("find_program caches results") {
        subproc::find_program_clear_cache();
        const auto first = subproc::find_program("echo");
        const auto second = subproc::find_program("echo");
        CHECK_EQ(first, second);
    }

    SUBCASE("can sleep") {
        subproc::StopWatch sw{};
        subproc::sleep_seconds(1.0);
        const auto delta = sw.seconds() - 1.0;
        CHECK(std::abs(delta) <= 0.1);
    }

    SUBCASE("stopwatch measures elapsed time") {
        subproc::StopWatch sw{};
        subproc::sleep_seconds(0.5);
        CHECK(sw.seconds() >= 0.4);
        CHECK(sw.seconds() <= 0.7);
    }

    SUBCASE("escape_shell_arg with special characters") {
        const auto escaped = subproc::escape_shell_arg("hello world");
        CHECK(escaped.contains("\""));

        const auto no_escape = subproc::escape_shell_arg("hello_world");
        CHECK(!no_escape.contains("\""));
    }

    SUBCASE("escape_shell_arg with escape disabled") {
        const auto result = subproc::escape_shell_arg("hello world", false);
        CHECK_EQ(result, "hello world");
    }

    SUBCASE("escape_shell_arg preserves safe characters") {
        CHECK_EQ(subproc::escape_shell_arg("abc123"), "abc123");
        CHECK_EQ(subproc::escape_shell_arg("file.txt"), "file.txt");
        CHECK_EQ(subproc::escape_shell_arg("path/to/file"), "path/to/file");
        CHECK_EQ(subproc::escape_shell_arg("a-b_c+d"), "a-b_c+d");
    }

    SUBCASE("get_cwd returns non-empty path") {
        const auto cwd = subproc::get_cwd();
        CHECK(!cwd.empty());
    }

    SUBCASE("CwdGuard restores working directory") {
        const auto original = subproc::get_cwd();
        {
            subproc::CwdGuard guard;
            subproc::set_cwd(std::filesystem::temp_directory_path().string());
            CHECK_NE(subproc::get_cwd(), original);
        }
        CHECK_EQ(subproc::get_cwd(), original);
    }

    SUBCASE("abspath handles relative paths") {
        const auto abs = subproc::abspath(".");
        CHECK(!abs.empty());
        // Result should be an absolute path
#ifdef _WIN32
        CHECK(abs.size() >= 2);
        CHECK(abs[1] == ':');
#else
        CHECK(abs[0] == '/');
#endif
    }

    SUBCASE("abspath with explicit relative base") {
        const auto abs = subproc::abspath("child", "/base/dir");
        CHECK(abs.contains("base"));
        CHECK(abs.contains("child"));
    }

    SUBCASE("current_env_copy returns populated map") {
        const auto env = subproc::current_env_copy();
        CHECK(!env.empty());
        const bool has_path = env.contains("PATH") || env.contains("Path");
        CHECK(has_path);
    }

    SUBCASE("EnvironSetter bool conversion") {
        CHECK(static_cast<bool>(subproc::cenv["PATH"]));
        CHECK_FALSE(static_cast<bool>(subproc::cenv["THIS_VAR_SHOULD_NOT_EXIST_12345"]));
    }

    SUBCASE("EnvironSetter int and bool assignment") {
        subproc::EnvGuard guard;
        subproc::cenv["TEST_INT_VAR"] = 42;
        CHECK_EQ(std::string(subproc::cenv["TEST_INT_VAR"]), "42");

        subproc::cenv["TEST_BOOL_VAR"] = true;
        CHECK_EQ(std::string(subproc::cenv["TEST_BOOL_VAR"]), "1");

        subproc::cenv["TEST_BOOL_VAR"] = false;
        CHECK_EQ(std::string(subproc::cenv["TEST_BOOL_VAR"]), "0");
    }

    SUBCASE("EnvironSetter float assignment") {
        subproc::EnvGuard guard;
        subproc::cenv["TEST_FLOAT_VAR"] = 3.14f;
        const auto val = std::string(subproc::cenv["TEST_FLOAT_VAR"]);
        CHECK(val.starts_with("3.14"));
    }

    SUBCASE("EnvironSetter nullptr clears variable") {
        subproc::EnvGuard guard;
        subproc::cenv["TEST_NULL_VAR"] = "value";
        CHECK_EQ(std::string(subproc::cenv["TEST_NULL_VAR"]), "value");
        subproc::cenv["TEST_NULL_VAR"] = nullptr;
        CHECK_EQ(std::string(subproc::cenv["TEST_NULL_VAR"]), "");
    }

    SUBCASE("CompletedProcess bool conversion") {
        const CompletedProcess success{.returncode = 0};
        CHECK(static_cast<bool>(success));

        const CompletedProcess failure{.returncode = 1};
        CHECK_FALSE(static_cast<bool>(failure));

        const CompletedProcess negative{.returncode = -1};
        CHECK_FALSE(static_cast<bool>(negative));
    }
}

// ============================================================================
// Pipe tests
// ============================================================================

TEST_CASE("TEST_CASE - pipe operations") {
    SUBCASE("can create and close pipes") {
        auto pp = subproc::pipe_create(false);
        CHECK(!!pp);
        pp.close();
    }

    SUBCASE("can write and read from pipe") {
        auto pp = subproc::pipe_create(false);
        const std::string msg = "hello pipe";
        const auto written = subproc::pipe_write(pp.output, msg.c_str(), msg.size());
        CHECK(written > 0);

        pp.close_output();

        const auto result = subproc::pipe_read_all(pp.input);
        CHECK_EQ(result, msg);
        pp.close_input();
    }

    SUBCASE("pipe handles binary data") {
        auto pp = subproc::pipe_create(false);
        const std::string binary_data = std::string("\x00\x01\x02\xff", 4);
        const auto written = subproc::pipe_write(pp.output, binary_data.c_str(), binary_data.size());
        CHECK_EQ(written, 4);
        pp.close_output();

        const auto result = subproc::pipe_read_all(pp.input);
        CHECK_EQ(result.size(), 4);
        pp.close_input();
    }

    SUBCASE("pipe_read_all on bad handle returns empty") {
        const auto result = subproc::pipe_read_all(subproc::kBadPipeValue);
        CHECK(result.empty());
    }

    SUBCASE("PipePair move semantics") {
        auto pp = subproc::pipe_create(false);
        auto pp2 = std::move(pp);
        CHECK(!!pp2);
        CHECK_EQ(pp.input, subproc::kBadPipeValue);
        CHECK_EQ(pp.output, subproc::kBadPipeValue);
        pp2.close();
    }

    SUBCASE("PipePair move assignment") {
        auto pp1 = subproc::pipe_create(false);
        auto pp2 = subproc::pipe_create(false);
        pp1 = std::move(pp2);
        CHECK(!!pp1);
        CHECK_EQ(pp2.input, subproc::kBadPipeValue);
        pp1.close();
    }

    SUBCASE("PipePair disown releases ownership") {
        auto pp = subproc::pipe_create(false);
        const auto in = pp.input;
        const auto out = pp.output;
        pp.disown();
        CHECK_EQ(pp.input, subproc::kBadPipeValue);
        CHECK_EQ(pp.output, subproc::kBadPipeValue);
        (void)subproc::pipe_close(in);
        (void)subproc::pipe_close(out);
    }

    SUBCASE("PipePair double close is safe") {
        auto pp = subproc::pipe_create(false);
        pp.close();
        pp.close(); // should not crash
    }

    SUBCASE("pipe_write_fully writes entire buffer") {
        auto pp = subproc::pipe_create(false);
        const std::string msg = "complete write test data";
        const auto written = subproc::pipe_write_fully(pp.output, msg.c_str(), msg.size());
        CHECK_EQ(written, static_cast<ssize_t>(msg.size()));
        pp.close_output();

        const auto result = subproc::pipe_read_all(pp.input);
        CHECK_EQ(result, msg);
        pp.close_input();
    }

    SUBCASE("pipe_peek_bytes reports available data") {
        auto pp = subproc::pipe_create(false);
        const std::string msg = "peek test";
        subproc::pipe_write(pp.output, msg.c_str(), msg.size());

        const auto available = subproc::pipe_peek_bytes(pp.input);
        CHECK_EQ(available, static_cast<ssize_t>(msg.size()));

        // Data should still be there after peek (not consumed)
        const auto available2 = subproc::pipe_peek_bytes(pp.input);
        CHECK_EQ(available, available2);

        pp.close();
    }

    SUBCASE("pipe_read_some reads available data without blocking") {
        auto pp = subproc::pipe_create(false);
        const std::string msg = "read_some test data here";
        subproc::pipe_write(pp.output, msg.c_str(), msg.size());

        char buf[256] = {};
        const auto n = subproc::pipe_read_some(pp.input, buf, sizeof(buf));
        CHECK(n > 0);
        CHECK(n <= static_cast<ssize_t>(msg.size()));
        CHECK_EQ(std::string(buf, static_cast<size_t>(n)), msg.substr(0, static_cast<size_t>(n)));

        pp.close();
    }

    SUBCASE("pipe_wait_for_read returns 1 when data available") {
        auto pp = subproc::pipe_create(false);
        subproc::pipe_write(pp.output, "data", 4);

        const int result = subproc::pipe_wait_for_read(pp.input, 1.0);
        CHECK_EQ(result, 1);

        pp.close();
    }

    SUBCASE("pipe_set_blocking toggles without error") {
        auto pp = subproc::pipe_create(false);
        CHECK(subproc::pipe_set_blocking(pp.input, false));
        CHECK(subproc::pipe_set_blocking(pp.input, true));
        pp.close();
    }

    SUBCASE("pipe_file opens and reads a file") {
        // Write a temp file, open it as a pipe handle, read it back
        const auto temp = std::filesystem::temp_directory_path() / "subprocess_pipe_file_test.txt";
        const std::string content = "pipe_file test content";
        {
            std::ofstream f(temp);
            f << content;
        }

        const auto handle = subproc::pipe_file(temp.string().c_str(), "r");
        CHECK_NE(handle, subproc::kBadPipeValue);

        const auto result = subproc::pipe_read_all(handle);
        CHECK_EQ(result, content);
        (void)subproc::pipe_close(handle);

        std::filesystem::remove(temp);
    }

    SUBCASE("pipe_file write mode creates file") {
        const auto temp = std::filesystem::temp_directory_path() / "subprocess_pipe_file_write_test.txt";
        const std::string content = "written via pipe_file";

        const auto handle = subproc::pipe_file(temp.string().c_str(), "w");
        CHECK_NE(handle, subproc::kBadPipeValue);
        subproc::pipe_write_fully(handle, content.c_str(), content.size());
        (void)subproc::pipe_close(handle);

        // Verify by reading back (scoped to close file before remove)
        {
            std::ifstream f(temp);
            const std::string readback((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            CHECK_EQ(readback, content);
        }

        std::filesystem::remove(temp);
    }

    SUBCASE("pipe_file returns kBadPipeValue for nonexistent file") {
        const auto handle = subproc::pipe_file("nonexistent_file_xyz_123.txt", "r");
        CHECK_EQ(handle, subproc::kBadPipeValue);
    }
}

// ============================================================================
// Popen tests
// ============================================================================

TEST_CASE("TEST_CASE - popen") {
    SUBCASE("can poll a subprocess") {
        subproc::EnvGuard guard;
        prepend_this_to_path();
        auto popen = RunBuilder({"sleep", "3"}).popen();
        subproc::StopWatch timer;

        int count = 0;
        while (!popen.poll())
            ++count;

        CHECK(count > 10);
        popen.close();

        const double timeout = timer.seconds();
        CHECK(timeout >= 2.5);
        CHECK(timeout <= 5.0);
    }

    SUBCASE("can run timeout") {
        subproc::EnvGuard guard;
        prepend_this_to_path();
        CHECK_THROWS_AS((void)subproc::run({"sleep", "3"}, {.new_process_group = true, .timeout = 1}),
                        subproc::TimeoutExpired);
    }

    SUBCASE("can wait timeout") {
        subproc::EnvGuard guard;
        prepend_this_to_path();
        auto popen = RunBuilder({"sleep", "10"}).new_process_group(true).popen();
        CHECK_THROWS_AS(popen.wait(3), subproc::TimeoutExpired);
        (void)popen.terminate();
        popen.close();
    }

    SUBCASE("try_wait returns nullopt on timeout") {
        subproc::EnvGuard guard;
        prepend_this_to_path();
        auto popen = RunBuilder({"sleep", "10"}).new_process_group(true).popen();
        const auto result = popen.try_wait(1);
        CHECK_FALSE(result.has_value());
        (void)popen.terminate();
        popen.close();
    }

    SUBCASE("try_wait returns value on completion") {
        auto popen = RunBuilder({"echo", "done"}).cout(PipeOption::pipe).popen();
        // Wait long enough for echo to finish
        const auto result = popen.try_wait(10);
        CHECK(result.has_value());
        CHECK_EQ(*result, 0);
        popen.close();
    }

    SUBCASE("can kill") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        auto popen = RunBuilder({"sleep", "10"}).popen();
        subproc::StopWatch timer;

        std::thread{[&] {
            subproc::sleep_seconds(3);
            (void)popen.kill();
        }}.detach();

        popen.close();
        const double timeout = timer.seconds();
        CHECK(std::abs(timeout - 3.0) <= 0.1);
    }

    SUBCASE("can terminate") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        auto popen = RunBuilder({"sleep", "10"}).new_process_group(true).popen();
        subproc::StopWatch timer;
        std::thread{[&] {
            subproc::sleep_seconds(3);
            (void)popen.terminate();
        }}.detach();

        popen.close();
        const double timeout = timer.seconds();
        CHECK(std::abs(timeout - 3.0) <= 0.1);
    }

    SUBCASE("can send SIGINT") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        auto popen = RunBuilder({"sleep", "10"}).new_process_group(false).popen();
        subproc::StopWatch timer;

        std::thread{[&] {
            subproc::sleep_seconds(3);
            (void)popen.send_signal(subproc::SigNum::PSIGINT);
        }}.detach();

        popen.close();
        const double timeout = timer.seconds();
        CHECK(timeout >= 2.5);
        CHECK(timeout <= 15.0);
    }

    SUBCASE("can pipe between two subprocesses") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        subproc::PipePair pp = subproc::pipe_create(false);
        CHECK(!!pp);

        subproc::Popen c = RunBuilder({"cat"}).cout(PipeOption::pipe).cin(pp.input).popen();
        subproc::Popen e = RunBuilder({"echo", "hello", "world"}).cout(pp.output).popen();
        pp.close();
        CompletedProcess p = subproc::run(c);
        e.close();
        c.close();
        CHECK_EQ(p.cout, "hello world" EOL);
    }

    SUBCASE("default Popen is invalid") {
        const Popen p;
        CHECK_EQ(p.cin, kBadPipeValue);
        CHECK_EQ(p.cout, kBadPipeValue);
        CHECK_EQ(p.cerr, kBadPipeValue);
        CHECK_EQ(p.pid, 0);
        CHECK_EQ(p.returncode, kBadReturnCode);
    }

    SUBCASE("popen move semantics") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        auto popen1 = RunBuilder({"echo", "hello"}).cout(PipeOption::pipe).popen();
        auto popen2 = std::move(popen1);
        CHECK_EQ(popen1.pid, 0);
        CHECK_NE(popen2.pid, 0);
        popen2.close();
    }

    SUBCASE("soft kill sends SIGTERM instead of SIGKILL") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        auto popen = RunBuilder({"sleep", "10"}).new_process_group(true).popen();
        popen.enable_soft_kill(true);
        subproc::StopWatch timer;
        std::thread{[&] {
            subproc::sleep_seconds(2);
            (void)popen.kill(); // with soft_kill, sends SIGTERM
        }}.detach();

        popen.close();
        CHECK(timer.seconds() >= 1.5);
    }
}

// ============================================================================
// subproc::run tests
// ============================================================================

TEST_CASE("TEST_CASE - subproc::run") {
    SUBCASE("can redirect subprocess output to CompletedProcess cout") {
        const auto cp = subproc::run({"echo", "hello", "world"}, RunBuilder().cout(PipeOption::pipe));

        CHECK_EQ(cp.cout, "hello world" EOL);
        CHECK(cp.cerr.empty());
        CHECK_EQ(cp.returncode, 0);
        CHECK_EQ(cp.args, CommandLine{"echo", "hello", "world"});
    }

    SUBCASE("can redirect subprocess output to CompletedProcess cerr") {
        const auto cp =
            subproc::run({"echo", "hello", "world"}, RunBuilder().cout(PipeOption::cerr).cerr(PipeOption::pipe));

        CHECK_EQ(cp.cerr, "hello world" EOL);
        CHECK(cp.cout.empty());
        CHECK_EQ(cp.returncode, 0);
    }

    SUBCASE("will throw on not found") {
        CHECK_THROWS_AS((void)subproc::run({"yay-322"}), subproc::CommandNotFoundError);
    }

    SUBCASE("can use C++20 designated initializers") {
        auto cp = subproc::run({"echo", "hello", "world"}, {.cout = PipeOption::pipe});
        CHECK_EQ(cp.cout, "hello world" EOL);
        CHECK(cp.cerr.empty());
        CHECK_EQ(cp.returncode, 0);

        cp = subproc::run({"echo", "hello", "world"}, {.cout = PipeOption::cerr, .cerr = PipeOption::pipe});
        CHECK_EQ(cp.cerr, "hello world" EOL);
        CHECK(cp.cout.empty());
    }

    SUBCASE("raise_on_nonzero throws CalledProcessError") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        CHECK_THROWS_AS(
            (void)subproc::run({"printenv", "NONEXISTENT_VAR_XYZ"}, {.cout = PipeOption::pipe, .raise_on_nonzero = true}),
            subproc::CalledProcessError);
    }

    SUBCASE("raise_on_nonzero does not throw on success") {
        const auto cp = subproc::run({"echo", "ok"}, {.cout = PipeOption::pipe, .raise_on_nonzero = true});
        CHECK_EQ(cp.returncode, 0);
        CHECK_EQ(cp.cout, "ok" EOL);
    }

    SUBCASE("can capture empty output") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        const auto cp = subproc::run({"echo"}, {.cout = PipeOption::pipe});
        CHECK_EQ(cp.returncode, 0);
        CHECK_EQ(cp.cout, EOL);
    }

    SUBCASE("can pass stdin data via string") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        const auto cp = subproc::run({"cat"}, RunBuilder().cin("piped input").cout(PipeOption::pipe));
        CHECK_EQ(cp.cout, "piped input");
    }

    SUBCASE("can capture both stdout and stderr simultaneously") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        subproc::cenv["USE_CERR"] = "1";
        const auto cp = subproc::run({"echo", "stderr_msg"},
                                        {.cout = PipeOption::pipe, .cerr = PipeOption::pipe, .env = current_env_copy()});
        CHECK_EQ(cp.cerr, "stderr_msg" EOL);
        CHECK(cp.cout.empty());
    }

    SUBCASE("run with custom cwd") {
        const auto temp = std::filesystem::temp_directory_path().string();
        const auto cp = subproc::run({"echo", "hello"}, {.cout = PipeOption::pipe, .cwd = temp});
        CHECK_EQ(cp.returncode, 0);
        CHECK_EQ(cp.cout, "hello" EOL);
    }

    SUBCASE("run with custom environment") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        auto env = subproc::current_env_copy();
        env["CUSTOM_TEST_VAR"] = "custom_value";
        const auto cp = subproc::run({"printenv", "CUSTOM_TEST_VAR"}, {.cout = PipeOption::pipe, .env = env});
        CHECK_EQ(cp.cout, "custom_value" EOL);
    }
}

// ============================================================================
// RunBuilder tests
// ============================================================================

TEST_CASE("TEST_CASE - subproc::RunBuilder") {
    SUBCASE("can redirect subprocess output to CompletedProcess cout") {
        const auto cp = RunBuilder({"echo", "hello", "world"}).cout(PipeOption::pipe).run();
        CHECK_EQ(cp.cout, "hello world" EOL);
        CHECK(cp.cerr.empty());
        CHECK_EQ(cp.returncode, 0);
    }

    SUBCASE("can redirect subprocess output to CompletedProcess cerr") {
        const auto cp = RunBuilder({"echo", "hello", "world"}).cout(PipeOption::cerr).cerr(PipeOption::pipe).run();

        CHECK_EQ(cp.cerr, "hello world" EOL);
        CHECK(cp.cout.empty());
        CHECK_EQ(cp.returncode, 0);
    }

    SUBCASE("can update env during runtime") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        subproc::EnvMap env = subproc::current_env_copy();
        CHECK(subproc::cenv["HELLO"].to_string().empty());
        env["HELLO"] = "world";
        CHECK(subproc::cenv["HELLO"].to_string().empty());

        const auto cp = RunBuilder({"printenv", "HELLO"}).cout(PipeOption::pipe).env(env).run();
        CHECK_EQ(cp.cout, "world" EOL);
    }

    SUBCASE("can redirect cerr to cout") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        subproc::cenv["USE_CERR"] = "1";
        subproc::find_program_clear_cache();

        auto cp = RunBuilder({"echo", "hello", "world"})
                      .cout(subproc::PipeOption::pipe)
                      .cerr(subproc::PipeOption::pipe)
                      .env(subproc::current_env_copy())
                      .run();

        CHECK_EQ(cp.cout, "");
        CHECK_EQ(cp.cerr, "hello world" EOL);

        const CommandLine args = {"echo", "hello", "world"};
        cp = RunBuilder(args).cerr(subproc::PipeOption::cout).cout(PipeOption::pipe).run();

        CHECK_EQ(cp.cout, "hello world" EOL);
        CHECK_EQ(cp.cerr, "");
    }

    SUBCASE("can redirect cout to cerr") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        const auto cp =
            RunBuilder({"echo", "hello", "world"}).cerr(subproc::PipeOption::pipe).cout(PipeOption::cerr).run();

        CHECK_EQ(cp.cout, "");
        CHECK_EQ(cp.cerr, "hello world" EOL);
    }

    SUBCASE("RunBuilder implicit conversion to RunOptions") {
        const RunOptions opts = RunBuilder({"echo"}).cout(PipeOption::pipe).raise_on_nonzero(true);
        CHECK(opts.raise_on_nonzero);
    }

    SUBCASE("RunBuilder chaining with all options") {
        const auto builder = RunBuilder({"echo", "test"})
                                 .cout(PipeOption::pipe)
                                 .cerr(PipeOption::pipe)
                                 .new_process_group(false)
                                 .create_no_window(false)
                                 .detached_process(false)
                                 .timeout(30.0)
                                 .raise_on_nonzero(false);

        const auto cp = builder.run();
        CHECK_EQ(cp.returncode, 0);
        CHECK_EQ(cp.cout, "test" EOL);
    }

    SUBCASE("RunBuilder popen creates async process") {
        auto popen = RunBuilder({"echo", "async"}).cout(PipeOption::pipe).popen();
        CHECK_NE(popen.pid, 0);
        const auto result = pipe_read_all(popen.cout);
        popen.close();
        CHECK_EQ(result, "async" EOL);
    }
}

// ============================================================================
// Error handling tests
// ============================================================================

TEST_CASE("TEST_CASE - error handling") {
    SUBCASE("CommandNotFoundError has message") {
        try {
            (void)subproc::run({"nonexistent_command_xyz"});
            CHECK(false);
        } catch (const subproc::CommandNotFoundError& e) {
            const std::string msg = e.what();
            CHECK(msg.contains("nonexistent_command_xyz"));
        }
    }

    SUBCASE("CalledProcessError contains details") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        try {
            (void)subproc::run({"printenv", "NONEXISTENT"}, {.cout = PipeOption::pipe, .raise_on_nonzero = true});
            CHECK(false);
        } catch (const subproc::CalledProcessError& e) {
            CHECK_NE(e.returncode, 0);
            CHECK(!e.cmd.empty());
        }
    }

    SUBCASE("TimeoutExpired contains timeout value") {
        subproc::EnvGuard guard;
        prepend_this_to_path();

        try {
            (void)subproc::run({"sleep", "10"}, {.new_process_group = true, .timeout = 1});
            CHECK(false);
        } catch (const subproc::TimeoutExpired& e) {
            CHECK(e.timeout == 1.0);
            CHECK(!e.cmd.empty());
        }
    }

    SUBCASE("exception hierarchy is correct") {
        // CalledProcessError -> SubprocError -> runtime_error
        subproc::EnvGuard guard;
        prepend_this_to_path();
        try {
            (void)subproc::run({"printenv", "NONEXISTENT"}, {.cout = PipeOption::pipe, .raise_on_nonzero = true});
        } catch (const subproc::SubprocError&) {
            CHECK(true); // caught as base class
        }

        try {
            (void)subproc::run({"nonexistent_xyz_123"});
        } catch (const std::runtime_error&) {
            CHECK(true); // caught as runtime_error
        }
    }
}

TEST_SUITE_END;

// ============================================================================
// Signal handler and main
// ============================================================================

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

BOOL CtrlHandler(DWORD fdwCtrlType) {
    if (fdwCtrlType == CTRL_C_EVENT) {
        std::println("basic_test: Ctrl+C received. Ignoring it.");
        return TRUE;
    }
    return FALSE;
}

#else
#include <csignal>

volatile std::sig_atomic_t g_signal_received = false;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::println("Ctrl+C received. Cleaning up and exiting.");
        g_signal_received.test_and_set();
    }
}
#endif

int main(int argc, char** argv) {
#if defined(_WIN32) || defined(_WIN64)
    if (SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CtrlHandler), TRUE))
#else
    if (std::signal(SIGINT, signal_handler) != SIG_ERR)
#endif
    {
        g_exe_dir = std::filesystem::path{subproc::abspath(argv[0])}.parent_path().string();
        std::string path = g_exe_dir + subproc::kPathDelimiter + std::string(subproc::cenv["PATH"]);
        subproc::cenv["PATH"] = path;
        return doctest::Context(argc, argv).run();
    }

    return 1;
}
