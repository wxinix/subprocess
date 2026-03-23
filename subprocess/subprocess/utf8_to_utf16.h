#pragma once
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace subprocess {

std::u16string utf8_to_utf16(const std::string& input);
std::string utf16_to_utf8(const std::u16string& input);
std::wstring utf8_to_utf16_w(const std::string& input);
std::string utf16_to_utf8(const std::wstring& input);

// Unified strlen16 via template - eliminates two identical overloads
template<typename CharT>
    requires(std::same_as<CharT, char16_t> || std::same_as<CharT, wchar_t>)
size_t strlen16(const CharT* input) {
    size_t size = 0;
    for (; *input; ++input)
        ++size;
    return size;
}

#ifdef _WIN32
std::string lptstr_to_string(LPTSTR input);
#endif

} // namespace subprocess
