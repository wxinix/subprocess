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
#include <cerrno>
#endif

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <ranges>

#include "builder.h"

namespace subprocess {

namespace {

#ifdef _WIN32
constexpr bool is_drive(const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
#endif

std::string clean_path(std::string path) {
    std::ranges::replace(path, '\\', '/');

#ifdef _WIN32
    if (path.size() == 2 && is_drive(path[0]) && path[1] == ':') path += '/';
#endif

    while (path.ends_with("//"))
        path.pop_back();

    return path;
}

bool is_absolute_path(const std::string& path) {
    if (path.empty()) return false;

#ifdef _WIN32
    return path.size() >= 2 && is_drive(path[0]) && path[1] == ':';
#else
    return path[0] == '/';
#endif
}

// Uses error_code overload to avoid try-catch for control flow
bool is_file(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

bool is_python3(const std::string& path) {
    const auto process = subprocess::run({path, "--version"}, {.cout = PipeOption::pipe, .cerr = PipeOption::cout});
    return process.cout.contains("3.");
}

std::string join_path(std::string parent, std::string child) {
    parent = clean_path(parent);
    child = clean_path(child);

    if (parent.empty() || child.empty() || child == ".") return parent;

    while (child.starts_with("./"))
        child.erase(0, 2);

    if (parent.back() != '/') parent += '/';
    if (child.front() == '/') child.erase(0, 1);

    return parent + child;
}

std::vector<std::string> split_path(const std::string& s) {
    if (s.empty()) return {};

    return s | std::views::split(kPathDelimiter)
             | std::views::transform([](auto&& r) { return std::string(r.begin(), r.end()); })
             | std::ranges::to<std::vector>();
}

std::string try_exe(const std::string& path) {
    if (is_file(path)) return path;

#ifdef _WIN32
    std::string path_ext = getenv("PATHEXT");
    if (path_ext.empty()) path_ext = "exe";

    for (const auto& ext : split_path(path_ext)) {
        if (ext.empty()) continue;
        if (const auto test_path = path + ext; is_file(test_path)) return test_path;
    }
#endif

    return {};
}

std::mutex g_program_cache_mutex;
std::map<std::string, std::string> g_program_cache;

std::string find_program_in_path(const std::string& name) {
    if (name.empty()) return {};

    std::scoped_lock lock(g_program_cache_mutex);

    if (name.size() >= 2 && (is_absolute_path(name) || name.starts_with("./") || name[0] == '/')) {
        if (is_file(name)) return abspath(name);
        if (const auto p = try_exe(name); !p.empty() && is_file(p)) return abspath(p);
    }

    if (g_program_cache.contains(name)) return g_program_cache[name];

    for (const auto& dir : split_path(getenv("PATH"))) {
        if (dir.empty()) continue;

        if (const auto p = try_exe(std::format("{}/{}", dir, name)); !p.empty() && is_file(p)) {
            g_program_cache[name] = p;
            return p;
        }
    }

    return {};
}

} // end of anonymous namespace

std::string abspath(std::string dir, std::string relative) {
    dir = clean_path(dir);

    if (is_absolute_path(dir)) return dir;

    if (relative.empty()) relative = subprocess::get_cwd();
    if (!is_absolute_path(relative)) relative = join_path(subprocess::get_cwd(), relative);

    return join_path(relative, dir);
}

std::string escape_shell_arg(const std::string& arg, bool escape) {
    if (!escape) return arg;

    const bool needs_quote = std::ranges::any_of(arg, [](const char c) {
        return !std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-' && c != '+' && c != '/';
    });

    if (!needs_quote) return arg;

    std::string result;
    result.reserve(arg.size() + 4);
    result += '\"';
    for (const char ch : arg) {
        if (ch == '\"' || ch == '\\') result += '\\';
        result += ch;
    }
    result += '\"';
    return result;
}

std::string get_cwd() {
    return std::filesystem::current_path().string();
}

void set_cwd(const std::string& path) {
    std::filesystem::current_path(path);
}

std::string getenv(const std::string& name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    (void)_dupenv_s(&value, &len, name.c_str());
    const std::string result = value ? std::string{value} : std::string{};
    free(value);
    return result;
#else
    const char* value = std::getenv(name.c_str());
    return value ? std::string{value} : std::string{};
#endif
}

std::string find_program(const std::string& name) {
    if (name != "python3") return find_program_in_path(name);

    const auto result = find_program_in_path("python");
    return (!result.empty() && is_python3(result)) ? result : std::string{};
}

void find_program_clear_cache() {
    std::scoped_lock lock(g_program_cache_mutex);
    g_program_cache.clear();
}

} // namespace subprocess
