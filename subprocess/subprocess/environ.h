#pragma once

#include <string>

#include "basic_types.hpp"
#include "shellutils.h"

namespace subprocess
{
class EnvironSetter
{
public:
    EnvironSetter(const std::string& name); // NOLINT(*-explicit-constructor)

    operator std::string() const; // NOLINT(*-explicit-constructor)
    explicit operator bool() const;
    [[nodiscard]] std::string to_string() const;

    EnvironSetter& operator=(const std::string& str);
    EnvironSetter& operator=(const char* str);
    EnvironSetter& operator=(std::nullptr_t);
    EnvironSetter& operator=(int value);
    EnvironSetter& operator=(bool value);
    EnvironSetter& operator=(float value);

private:
    std::string m_name;
};

/** Used for working with environment variables */
class Environ
{
public:
    EnvironSetter operator[](const std::string&);
};

/**
 * Makes it easy to get/set environment variables.
 * e.g. like so `subprocess::cenv["VAR"] = "Value";`
 */
extern Environ cenv;

/** Creates a copy of current environment variables and returns the map */
EnvMap current_env_copy();

/**
  Gives an environment block used in Windows APIs. Each item is null
  terminated, end of list is double null-terminated and conforms to
  expectations of various windows API.
 */
std::u16string create_env_block(const EnvMap& map);

/**
 * Use this to put a guard for changing current working directory. On
 * destruction the current working directory will be reset to the old one.
 */
class CwdGuard
{
public:
    CwdGuard()
    {
        m_cwd = subprocess::getcwd();
    }

    ~CwdGuard()
    {
        subprocess::setcwd(m_cwd);
    }

private:
    std::string m_cwd;
};

/**
 * On destruction reset environment variables and current working directory
 * to as it was on construction.
 */
class [[maybe_unused]] EnvGuard : public CwdGuard
{
public:
    EnvGuard()
    {
        m_env = current_env_copy();
    }

    ~EnvGuard();

private:
    EnvMap m_env;
};

} // namespace subprocess