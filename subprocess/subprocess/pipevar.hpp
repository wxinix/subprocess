#pragma once

#include <cstdio>
#include <iostream>
#include <string>
#include <variant>

#include "basic_types.hpp"

namespace subprocess
{

// Enum class to represent different types in the PipeVar variant
enum class PipeVarIndex
{
    option,
    string,
    handle,
    istream,
    ostream,
    file
};

// Type alias for the PipeVar variant
typedef std::variant<PipeOption, std::string, PipeHandle, std::istream*, std::ostream*, FILE*> PipeVar;

/**
 * @brief Gets the PipeOption from the PipeVar variant.
 *
 * This function retrieves the PipeOption value from the given PipeVar variant.
 *
 * @param option The PipeVar variant containing different types.
 * @return The corresponding PipeOption value.
 */
inline PipeOption get_pipe_option(const PipeVar& option)
{
    // Determine the type of the PipeVar using the index
    auto index = static_cast<PipeVarIndex>(option.index());
    PipeOption result;

    // Switch based on the type and extract the appropriate value
    switch (index)
    {
    case PipeVarIndex::option:
        result = std::get<PipeOption>(option);
        break;

    case PipeVarIndex::handle:
        result = PipeOption::specific;
        break;

    default:
        result = PipeOption::pipe;
        break;
    }

    return result;
}


} // namespace subprocess