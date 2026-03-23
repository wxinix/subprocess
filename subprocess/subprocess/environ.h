#pragma once

#include <mutex>
#include <string>
#include <type_traits>

#include "basic_types.hpp"
#include "shellutils.h"

namespace subprocess {
class EnvironSetter {
public:
    EnvironSetter(std::string name); // NOLINT (*-explicit-constructor)

    operator std::string() const; // NOLINT (*-explicit-constructor)
    explicit operator bool() const;
    [[nodiscard]] std::string to_string() const;

    EnvironSetter& operator=(const std::string& str);
    EnvironSetter& operator=(const char* str);
    EnvironSetter& operator=(std::nullptr_t);

    // Single template replaces separate int/bool/float overloads
    template<typename T>
        requires std::is_arithmetic_v<T>
    EnvironSetter& operator=(const T value) {
        if constexpr (std::is_same_v<T, bool>)
            return *this = std::string{value ? "1" : "0"};
        else
            return *this = std::to_string(value);
    }

private:
    const std::string m_name;
};

struct EnvLock {
    EnvLock() : m_guard(mtx) {}

private:
    static inline std::mutex mtx{};
    std::scoped_lock<std::mutex> m_guard;
};

class Environ {
public:
    EnvironSetter operator[](const std::string& name) const;
};

extern Environ cenv;

EnvMap current_env_copy();
std::u16string create_env_block(const EnvMap& map);

class CwdGuard {
public:
    CwdGuard() : m_cwd(subprocess::get_cwd()) {}
    ~CwdGuard() { subprocess::set_cwd(m_cwd); }

    CwdGuard(const CwdGuard&) = delete;
    CwdGuard& operator=(const CwdGuard&) = delete;

private:
    const std::string m_cwd;
};

class EnvGuard : public CwdGuard {
public:
    EnvGuard() : m_env(current_env_copy()) {}
    ~EnvGuard();

    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

private:
    const EnvMap m_env;
};
} // namespace subprocess
