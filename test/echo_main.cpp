#include <cstdio>
#include <cstring>
#include <print>

#include <subproc.hpp>

int main(int argc, char** argv) {
    std::string use_cerr_str = subproc::cenv["USE_CERR"];
    bool use_cerr = use_cerr_str == "1";
    auto output_file = use_cerr ? stderr : stdout;
    bool print_space = false;

    for (int i = 1; i < argc; ++i) {
        if (print_space) fwrite(" ", 1, 1, output_file);
        fwrite(argv[i], 1, strlen(argv[i]), output_file);
        print_space = true;
    }

    fwrite("\n", 1, 1, output_file);
    return 0;
}
