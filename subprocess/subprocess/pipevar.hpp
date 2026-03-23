#pragma once

#include <cstdio>
#include <iosfwd>
#include <string>
#include <variant>

#include "basic_types.hpp"

namespace subprocess {

// Helper for std::visit pattern matching
template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

// Type alias for the PipeVar variant
using PipeVar = std::variant<PipeOption, std::string, PipeHandle, std::istream*, std::ostream*, FILE*>;

/** @brief Extracts the logical PipeOption from a PipeVar. */
inline PipeOption get_pipe_option(const PipeVar& var) {
    return std::visit(overloaded{
                          [](PipeOption opt) { return opt; },
                          [](PipeHandle) { return PipeOption::specific; },
                          [](const auto&) { return PipeOption::pipe; },
                      },
                      var);
}

} // namespace subprocess
