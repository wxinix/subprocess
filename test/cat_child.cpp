#include <algorithm>
#include <iostream>
#include <ranges>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

enum ExitCode {
    success = 0,
    cout_fail,
    cerr_fail,
    cout_cerr_fail,
    exception_thrown
};

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    bool output_err = std::ranges::contains(args, std::string_view{"--output-stderr"});

    std::vector<char> buffer(2048);

    try {
        while (true) {
            auto transfered = _read(0, buffer.data(), static_cast<unsigned int>(buffer.size()));
            if (transfered == 0) break;

            if (output_err) {
                std::cerr.write(buffer.data(), transfered);
                std::cerr.flush();
            } else {
                std::cout.write(buffer.data(), transfered);
                std::cout.flush();
            }
        }
    } catch (...) { return ExitCode::exception_thrown; }

    if (std::cout.fail() && std::cerr.fail()) return ExitCode::cout_cerr_fail;
    if (std::cout.fail()) return ExitCode::cout_fail;
    if (std::cerr.fail()) return ExitCode::cerr_fail;

    return ExitCode::success;
}
