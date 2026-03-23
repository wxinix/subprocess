#include "environ.h"

#include <format>
#include <string_view>

#include "utf8_to_utf16.h"

#include <ranges>

#ifndef _WIN32
extern "C" char** environ;
#endif

namespace subprocess {
Environ cenv;

EnvironSetter::EnvironSetter(std::string name) : m_name(std::move(name)) {}

EnvironSetter::operator std::string() const {
    return to_string();
}

EnvironSetter::operator bool() const {
    return !m_name.empty() && !subprocess::getenv(m_name).empty();
}

std::string EnvironSetter::to_string() const {
    return subprocess::getenv(m_name);
}

EnvironSetter& EnvironSetter::operator=(const std::string& str) {
    return *this = str.c_str();
}

EnvironSetter& EnvironSetter::operator=(const char* str) {
    if (m_name == "PATH" || m_name == "Path" || m_name == "path") find_program_clear_cache();

#ifdef _WIN32
    (void)_putenv_s(m_name.c_str(), str ? str : "");
#else
    if (!str || !*str)
        unsetenv(m_name.c_str());
    else
        setenv(m_name.c_str(), str, true);
#endif
    return *this;
}

EnvironSetter& EnvironSetter::operator=(std::nullptr_t) {
    return *this = static_cast<const char*>(nullptr);
}


EnvironSetter Environ::operator[](const std::string& name) const {
    return EnvironSetter{name};
}

// Helper: parse "NAME=VALUE" into map, shared by both platforms
static void parse_env_entry(const std::string_view entry, EnvMap& out) {
    if (const auto eq = entry.find('='); eq != std::string_view::npos && eq > 0)
        out[std::string(entry.substr(0, eq))] = std::string(entry.substr(eq + 1));
}

EnvGuard::~EnvGuard() {
    const auto new_env = current_env_copy();

    for (const auto& name : new_env | std::views::keys) {
        if (!m_env.contains(name)) cenv[name] = nullptr;
    }

    for (const auto& [name, value] : m_env) {
        if (!new_env.contains(name) || value != new_env.at(name)) cenv[name] = value;
    }
}

EnvMap current_env_copy() {
    EnvMap result;

#ifdef _WIN32
    const auto env_block = GetEnvironmentStringsW();
    if (!env_block) return result;

    for (auto list = env_block; *list; list += strlen16(list) + 1) {
        parse_env_entry(utf16_to_utf8(list), result);
    }

    (void)FreeEnvironmentStringsW(env_block);
#else
    for (char** list = environ; *list; ++list) {
        parse_env_entry(*list, result);
    }
#endif

    return result;
}

std::u16string create_env_block(const EnvMap& map) {
    std::u16string result;

    size_t size = 1;
    for (const auto& [name, value] : map)
        size += name.size() + value.size() + 2;
    result.reserve(size * 2);

    for (const auto& [name, value] : map) {
        result += utf8_to_utf16(std::format("{}={}", name, value));
        result += char16_t{'\0'};
    }

    result += char16_t{'\0'};
    return result;
}
} // namespace subprocess
