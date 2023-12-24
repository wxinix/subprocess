#include "shellutils.h"
#include "basic_types.hpp"

#ifndef _WIN32
#include <spawn.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include <errno.h>
#endif

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>

#include "builder.h"

namespace subprocess
{

namespace
{

#ifdef _WIN32
/**
 * @brief Checks if the given character represents a valid drive letter on Windows.
 *
 * On Windows, drive letters are represented by uppercase or lowercase alphabetical characters.
 * This function returns true if the character is a valid drive letter, and false otherwise.
 *
 * @param c The character to raise_on_nonzero.
 * @return true if the character is a valid drive letter, false otherwise.
 */
bool is_drive(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
#endif

/**
 * @brief Cleans the provided path by replacing backslashes with forward slashes
 *        and ensuring proper formatting for drive letters on Windows.
 *
 * This function improves the consistency and formatting of file paths.
 *
 * @param path The path to be cleaned.
 * @return The cleaned path.
 */
std::string clean_path(std::string path)
{
    // Replace backslashes with forward slashes
    for (char& i : path)
    {
        if (i == '\\')
        {
            i = '/';
        }
    }

#ifdef _WIN32
    // On Windows, add a trailing slash after a drive letter if it doesn't exist
    if (path.size() == 2U)
    {
        if (is_drive(path[0U]) && path[1U] == ':')
        {
            path += '/';
        }
    }
#endif

    // Remove consecutive forward slashes at the end.
    while (path.size() >= 2U && path[path.size() - 1U] == '/' && path[path.size() - 2U] == '/')
    {
        path.resize(path.size() - 1U);
    }

    return path;
}

/**
 * @brief Checks if the given path is an absolute path.
 *
 * This function determines whether the provided path is an absolute path.
 * On Windows, an absolute path can start with a drive letter followed by a colon,
 * while on Unix-like systems, it must start with a '/'.
 *
 * @param path The path to raise_on_nonzero.
 * @return true if the path is absolute, false otherwise or if the path is empty.
 */
bool is_absolute_path(const std::string& path)
{
    bool result = false;

    if (path.empty()) // An empty path is not absolute
    {
        result = false;
    }

#ifdef _WIN32
    // On Windows, check if the path starts with a drive letter followed by a colon
    if (is_drive(path[0U]) && path[1U] == ':')
    {
        result = true;
    }
#else
    // On Unix-like systems, check if the path starts with a '/'
    if (path[0U] == '/')
    {
        result = true;
    }
#endif

    // If none of the conditions are met, the path is not absolute
    return result;
}

/**
 * @brief Checks if a regular file exists at the specified path.
 *
 * A regular file is a common type of file that stores data in a standard manner.
 * It excludes directories, symbolic links, and other special file types.
 *
 * @param path The path to the file. If the path is relative path, then it will be
 * relative to the current working directory.
 * @return true if a regular file exists at the specified path, false otherwise
 * or if the path is empty.
 */
bool is_file(const std::string& path)
{
    bool result;

    try
    {
        result = !path.empty() && std::filesystem::is_regular_file(path);
    }
    catch (std::filesystem::filesystem_error& /* e */)
    {
        result = false;
    }

    return result;
}

/**
 * @brief Checks if the specified executable at the given path is Python 3.
 *
 * This function runs the specified executable with the "--version" argument using a subprocess
 * and examines the output to determine if it represents Python 3.
 *
 * @param path The path to the Python executable.
 * @return True if the executable is Python 3, false otherwise.
 *
 * @note The following alternative syntax examples can be used (last one only applicable with C++20):
 *   \code{cpp}
 *   auto process = subprocess::run({path, "--version"}, RunBuilder().cout(PipeOption::pipe).cerr(PipeOption::cout));
 *   \endcode
 *   or
 *   \code{cpp}
 *   auto process = subprocess::RunBuilder({path, "--version"}).cout(PipeOption::pipe).cerr(PipeOption::cout).run();
 *   \endcode
 *   or
 *   \code{cpp}
 *   auto process = subprocess::run({path, "--version"}, {.cout = PipeOption::pipe, .cerr = PipeOption::cout});
 *   \endcode
 */
bool is_python3(const std::string& path)
{
    auto process = subprocess::run({path, "--version"}, {.cout = PipeOption::pipe, .cerr = PipeOption::cout});
    return process.cout.find("3.") != std::string::npos; // std::string::contains() only available with C++23
}

/**
 * @brief Joins a parent path and a child path, ensuring proper formatting.
 *
 * This function combines a parent path and a child path, handling various cases to ensure
 * the resulting path is properly formatted and free of unnecessary elements.
 *
 * @param parent The parent path.
 * @param child The child path to join with the parent.
 * @return The joined and cleaned path.
 */
std::string join_path(std::string parent, std::string child)
{
    // Clean both parent and child paths
    parent = clean_path(parent);
    child = clean_path(child);

    if (!parent.empty() && !child.empty() && child != ".")
    {
        // Check for potential programmer error if the child path contains ':'
        if (child.find(':') != std::string::npos)
        {
            // Note: Consider logging a warning or throwing an exception in a real-world scenario
        }
        // Remove leading "./" components from the child path
        while (child.size() >= 2U && child.substr(0U, 2U) == "./")
        {
            child = child.substr(2U);
        }
        // Make sure parent has the last char being '/'
        if (parent.back() != '/')
        {
            parent += '/';
        }
        // Remove the leading '/' from child, if any.
        if (child.front() == '/')
        {
            child = child.substr(1U);
        }
        parent += child;
    }

    return parent;
}

/**
 * @brief Splits a string into a vector of substrings using a specified delimiter.
 *
 * @param s The input string to be split.
 * @param delim The delimiter character to split the string.
 * @return A vector of substrings.
 */
std::vector<std::string> split(const std::string& s, const char& delim)
{
    std::vector<std::string> result;
    std::istringstream ss{s};
    std::string item;

    while (std::getline(ss, item, delim))
    {
        result.push_back(item);
    }

    // Check if std::getline failed to extract any tokens
    if (result.empty() && !s.empty())
    {
        result.push_back(s);
    }

    return result;
}

/**
 * @brief Attempts to find the executable file at the specified path.
 *
 * This function checks if the executable file exists at the given path. If not,
 * it iterates through common executable file extensions (e.g., ".exe" on Windows)
 * and checks for the existence of the file with those extensions.
 *
 * @param path The path to the executable file.
 * @return The path to the executable file if found, an empty string otherwise.
 */
std::string try_exe(const std::string& path)
{
    std::string result{};

    if (is_file(path))
    {
        result = path;
    }
#ifdef _WIN32
    else
    { // PATHEXT defines the list of file extensions considered executable.
        std::string path_ext = getenv("PATHEXT");
        if (path_ext.empty())
        {
            path_ext = "exe";
        }

        for (const std::string& ext : split(path_ext, kPathDelimiter))
        {
            if (!ext.empty())
            {
                if (const auto test_path = path + ext; is_file(test_path))
                {
                    result = test_path;
                    break;
                }
            }
        }
    }
#endif

    return result;
}

std::mutex g_program_cache_mutex;
std::map<std::string, std::string> g_program_cache;

/**
 * @brief Finds the absolute path of an executable program in the system's PATH.
 *
 * This function searches for the specified program name in the system's PATH
 * environment variable and returns the absolute path if found. It utilizes a
 * cache to improve performance by storing previously resolved program paths.
 *
 * @param name The name of the program to find.
 * @return The absolute path of the program if found, an empty string otherwise.
 */
std::string find_program_in_path(const std::string& name)
{
    std::tuple<bool, std::string> result{false, ""};             // {found, value}
    std::unique_lock lock(g_program_cache_mutex);                // For cache variable thread-safety
    std::map<std::string, std::string>& cache = g_program_cache; // Ref to global cache

    if (!name.empty())
    {
        if (name.size() >= 2U && (is_absolute_path(name) || (name[0U] == '.' && name[1U] == '/') || name[0U] == '/'))
        {
            if (is_file(name))
            {
                result = {true, abspath(name)};
            }
            else
            {
                if (auto p = try_exe(name); !p.empty() && is_file(p))
                {
                    result = {true, abspath(p)};
                }
            }
        }

        if (auto [found, value] = result; !found)
        {
            if (cache.contains(name))
            {
                result = {true, cache[name]}; // already cached
            }
            else
            {
                for (std::string p : split(getenv("PATH"), kPathDelimiter))
                {
                    if (p.empty())
                    {
                        continue; // for the purpose of limiting levels of nested if
                    }

                    if (p = try_exe(std::format("{}/{}", p, name)); !p.empty() && is_file(p))
                    {
                        cache[name] = p; // update cache
                        result = {true, p};
                        break;
                    }
                }
            }
        }
    }
    return std::get<1>(result);
}

} // end of anonymous namespace

std::string abspath(std::string dir, std::string relative)
{
    std::string result{};

    if (dir = clean_path(dir); is_absolute_path(dir))
    {
        result = dir;
    }
    else
    {
        if (relative.empty())
        {
            relative = subprocess::get_cwd();
        }

        if (!is_absolute_path(relative))
        {
            relative = join_path(subprocess::get_cwd(), relative);
        }

        result = join_path(relative, dir);
    }

    return result;
}

std::string escape_shell_arg(const std::string& arg)
{
    // Check if quoting is necessary
    bool needs_quote = false;
    for (const char c : arg)
    {
        // Alrightlist: alphanumeric, dot, underscore, hyphen, plus, slash
        if (0 != std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == '+' || c == '/')
        {
            needs_quote = true;
            break;
        }
    }

    // Result string to be returned
    std::string result;

    // If quoting is not needed, return the original argument
    if (!needs_quote)
    {
        result = arg;
    }
    else
    {
        // If quoting is needed, perform escaping and add quotes
        result = "\""; // Opening double quotes
        for (const char ch : arg)
        {
            // Escape double quotes and backslashes
            if (ch == '\"' || ch == '\\')
            {
                result += '\\';
            }

            result += ch;
        }
        result += "\""; // Closing double quotes.
    }

    return result;
}

std::string get_cwd()
{
    return std::filesystem::current_path().string();
}

void set_cwd(const std::string& path)
{
    std::filesystem::current_path(path);
}

std::string getenv(const std::string& name)
{
    char* value = nullptr;
    std::string result{};

#ifdef _WIN32
    size_t len = 0U;
    (void)_dupenv_s(static_cast<char**>(&value), &len, name.c_str());
#else
    value = std::getenv(name.c_str());
#endif
    result = (value == nullptr) ? "" : std::string{value};

#ifdef _WIN32
    free(value);
#endif
    return result;
}

std::string find_program(const std::string& name)
{
    std::string result{};

    if (name != "python3")
    {
        result = find_program_in_path(name);
    }
    else
    {
        result = is_python3(result = find_program_in_path("python")) ? result : "";
    }

    return result;
}

void find_program_clear_cache()
{
    std::unique_lock<std::mutex> lock(g_program_cache_mutex);
    g_program_cache.clear();
}

} // namespace subprocess