#pragma once
#include <string>

namespace subprocess {

std::string getenv(const std::string& name);
std::string find_program(const std::string& name);
void find_program_clear_cache();
std::string escape_shell_arg(const std::string& arg, bool escape = true);
std::string get_cwd();
void set_cwd(const std::string& path);
std::string abspath(std::string dir, std::string relative = "");

} // namespace subprocess
