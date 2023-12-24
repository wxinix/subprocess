#include "environ.h"

#include <cstdlib>
#include <format>
#include <string>

#include "utf8_to_utf16.h"

#ifndef _WIN32
extern "C" char** environ;
#endif

namespace subprocess
{
using std::to_string;

Environ cenv;

EnvironSetter::EnvironSetter(const std::string& name)
{
    m_name = name;
}

EnvironSetter::operator std::string() const
{
    return to_string();
}

EnvironSetter::operator bool() const
{
    return !m_name.empty() && !subprocess::getenv(m_name).empty();
}

std::string EnvironSetter::to_string() const
{
    return subprocess::getenv(m_name);
}

EnvironSetter& EnvironSetter::operator=(const std::string& str)
{
    return *this = str.c_str();
}

EnvironSetter& EnvironSetter::operator=(const char* str)
{
    if (m_name == "PATH" || m_name == "Path" || m_name == "path")
    {
        find_program_clear_cache();
    }
#ifdef _WIN32
    // if it's empty windows deletes it.
    (void)_putenv_s(m_name.c_str(), nullptr != str ? str : ""); // Empty string include just null terminator.
#else
    if (str == nullptr || !*str)
    {
        unsetenv(m_name.c_str());
    }
    else
    {
        setenv(m_name.c_str(), str, true);
    }
#endif
    return *this;
}

EnvironSetter& EnvironSetter::operator=(std::nullptr_t)
{
    return *this = static_cast<const char*>(nullptr);
}

EnvironSetter& EnvironSetter::operator=(int value)
{
    return *this = std::to_string(value);
}

EnvironSetter& EnvironSetter::operator=(bool value)
{
    return *this = std::string{value ? "1" : "0"};
}

EnvironSetter& EnvironSetter::operator=(float value)
{
    return *this = std::to_string(value);
}

EnvironSetter Environ::operator[](const std::string& name)
{
    return EnvironSetter{name};
}

EnvGuard::~EnvGuard()
{
    auto new_env = current_env_copy();
    // Remove those not existing with the old env
    for (const auto& [name, value] : new_env)
    {
        if (!m_env.contains(name))
        {
            cenv[name] = nullptr;
        }
    }
    // Write back the old env. If new env has the same name and value, just skip.
    for (const auto& [name, value] : m_env)
    {
        if (!new_env.contains(name) || value != new_env[name])
        {
            cenv[name] = value;
        }
    }
}

EnvMap current_env_copy()
{
    EnvMap result{};
#ifdef _WIN32
    if (auto env_block = GetEnvironmentStringsW(); nullptr != env_block)
    {
        auto list = env_block;
        while (0U != *list)
        {
            std::string u8str = utf16_to_utf8(list);
            const char* name_start = u8str.c_str();
            const char* name_end = name_start;

            while ('=' != *name_end && '\0' != *name_end)
            {
                ++name_end;
            }

            if ('=' == *name_end && name_end != name_start)
            {
                const std::string name(name_start, name_end);
                const char* value = name_end + 1;
                result[name] = value;
            }

            list += strlen16(list) + 1U; // list gets updated here.
        }

        (void)FreeEnvironmentStringsW(env_block);
    }
#else
    for (char** list = environ; *list; ++list)
    {
        char* name_start = *list;
        char* name_end = name_start;

        while ('=' != *name_end && '\0' != *name_end)
        {
            ++name_end;
        }

        if ('=' == *name_end && name_end != name_start)
        {
            const std::string name(name_start, name_end);
            const char* value = name_end + 1;
            result[name] = value;
        }
    }
#endif
    return result;
}

std::u16string create_env_block(const EnvMap& map)
{
    size_t size = 0U;
    for (const auto& [name, value] : map)
    {
        size += name.size() + value.size() + 2U;
    }
    size += 1U;

    std::u16string result;
    result.reserve(size * 2U);

    for (const auto& [name, value] : map)
    {
        const std::string line = std::format("{}={}", name, value);
        result += utf8_to_utf16(line);
        result += static_cast<char16_t>('\0');
    }

    result += static_cast<char16_t>('\0');
    return result;
}

} // namespace subprocess