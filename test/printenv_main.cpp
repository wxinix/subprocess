#include <print>

#include <subproc.hpp>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::println("printenv <var-name>");
        std::println("    Will print out contents of that variable");
        std::println("    Returns failure code if variable was not found.");
        return 1;
    }

    std::string result = subproc::cenv[argv[1]];
    if (result.empty()) return 1;

    std::println("{}", result);
    return 0;
}
