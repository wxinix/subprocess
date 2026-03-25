#pragma once
#include <string>

namespace subprocess {

[[nodiscard]] std::string getenv(const std::string& name);
[[nodiscard]] std::string find_program(const std::string& name);
void find_program_clear_cache();
[[nodiscard]] std::string escape_shell_arg(const std::string& arg, bool escape = true);
[[nodiscard]] std::string get_cwd();
void set_cwd(const std::string& path);
[[nodiscard]] std::string abspath(std::string dir, std::string relative = "");

} // namespace subprocess
