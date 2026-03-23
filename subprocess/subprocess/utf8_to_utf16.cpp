#include "utf8_to_utf16.h"

#ifdef _WIN32
#include <memory>
#endif

namespace subprocess {

#ifndef _WIN32
std::u16string utf8_to_utf16(const std::string& str) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    return convert.from_bytes(str);
}

std::string utf16_to_utf8(const std::u16string& str) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    return convert.to_bytes(str);
}

std::wstring utf8_to_utf16_w(const std::string& str) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert;
    return convert.from_bytes(str);
}

std::string utf16_to_utf8(const std::wstring& str) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert;
    return convert.to_bytes(str);
}
#endif

#ifdef _WIN32

std::u16string utf8_to_utf16(const std::string& input) {
    if (input.empty()) return {};

    const auto size = input.size() + 1;
    const auto buffer = std::make_unique<wchar_t[]>(size);

    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), static_cast<int>(size),
                                      buffer.get(), static_cast<int>(size));

    return (n > 0) ? std::u16string{buffer.get(), buffer.get() + n - 1} : std::u16string{};
}

#ifdef __MINGW32__
constexpr int WC_ERR_INVALID_CHARS = 0;
#endif

std::string utf16_to_utf8(const std::u16string& input) {
    if (input.empty()) return {};

    const auto size = input.size() + 1;
    const auto* wide = reinterpret_cast<const wchar_t*>(input.c_str());

    const int r =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, static_cast<int>(size), nullptr, 0, nullptr, nullptr);

    if (r <= 0) return {};

    const auto buffer = std::make_unique<char[]>(static_cast<size_t>(r));
    const int m = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(size), buffer.get(), r, nullptr, nullptr);

    return (m > 0) ? std::string{buffer.get(), buffer.get() + m - 1} : std::string{};
}

std::string utf16_to_utf8(const std::wstring& input) {
    return utf16_to_utf8(std::u16string{reinterpret_cast<const char16_t*>(input.c_str()), input.size()});
}

std::string lptstr_to_string(LPTSTR input) { // NOLINT
    if (!input) return {};

#ifdef UNICODE
    return utf16_to_utf8(input);
#else
    return input;
#endif
}
#endif

} // namespace subprocess
