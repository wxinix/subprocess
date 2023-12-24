#pragma once
#include <string>

namespace subprocess
{

/**
 * Retrieves the value of the specified environment variable.
 * On Windows, it uses the secure function _dupenv_s, while on non-Windows,
 * it employs std::getenv.
 *
 * @param name The name of the environment variable.
 * @return The value of the environment variable.
 */
std::string getenv(const std::string& name);

/**
 * Searches for a program in the PATH environment variable.
 *
 * On Windows, it attempts to find executables by adding suffixes specified in
 * the PATHEXT environment variable. For example, on Windows, an input of
 * "python3" will also search for "python" executables and inspect their
 * version to identify an executable that offers Python 3.x.
 */
std::string find_program(const std::string& name);

/**
 * Clears the cache used by find_program.
 *
 * The find_program function uses an internal cache to locate executables. If
 * you modify PATH using subprocess::cenv, this cache will be automatically
 * cleared for you. However, if a new program is added to a folder with the same
 * name as an existing program, you may want to clear the cache explicitly so
 * that the new program is found as expected, rather than the old program being
 * returned.
 */
void find_program_clear_cache();

/**
 * Escapes the argument to make it suitable for use on the command line.
 * The purpose is to handle special characters or cases where quoting might
 * be necessary to ensure the argument is interpreted correctly by the shell.
 */
std::string escape_shell_arg(const std::string& arg);

/**
 * Retrieves the current working directory of the calling process.
 */
std::string getcwd();

/**
 * Sets the current working directory of the calling process.
 *
 * @param path The path to set as the current working directory.
 */
void setcwd(const std::string& path);

/**
 * Converts the provided directory path to an absolute path.
 *
 * @param dir The path to be converted to an absolute path.
 * @param relative If specified, use this path instead of the current working
 *        directory as the reference for making dir an absolute path.
 * @return The absolute path.
 */
std::string abspath(std::string dir, std::string relative = "");

} // namespace subprocess
