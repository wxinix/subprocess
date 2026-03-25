#include "utf8_to_utf16.h"

#ifdef _WIN32
#include <memory>
#endif

#include <cstdint>

namespace subprocess {

#ifndef _WIN32

// Hand-rolled UTF-8 <-> UTF-16 codec, replacing deprecated std::wstring_convert (C++17).

namespace {

uint32_t decode_utf8_codepoint(const std::string& s, size_t& i) {
    const auto ch = static_cast<uint8_t>(s[i]);

    if (ch < 0x80) { ++i; return ch; }

    uint32_t cp = 0;
    size_t extra = 0;

    if ((ch >> 5) == 0x6)       { cp = ch & 0x1F; extra = 1; }
    else if ((ch >> 4) == 0xE)  { cp = ch & 0x0F; extra = 2; }
    else if ((ch >> 3) == 0x1E) { cp = ch & 0x07; extra = 3; }
    else                        { ++i; return 0xFFFD; } // replacement char

    for (++i; extra > 0 && i < s.size(); --extra, ++i)
        cp = (cp << 6) | (static_cast<uint8_t>(s[i]) & 0x3F);

    return cp;
}

void encode_utf8(std::string& out, const uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void encode_utf16(std::u16string& out, const uint32_t cp) {
    if (cp <= 0xFFFF) {
        out.push_back(static_cast<char16_t>(cp));
    } else {
        const uint32_t adj = cp - 0x10000;
        out.push_back(static_cast<char16_t>(0xD800 + (adj >> 10)));
        out.push_back(static_cast<char16_t>(0xDC00 + (adj & 0x3FF)));
    }
}

uint32_t decode_utf16_surrogate(const char16_t* data, size_t size, size_t& i) {
    uint32_t cp = data[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < size) {
        const uint32_t lo = data[i + 1];
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            ++i;
        }
    }
    ++i;
    return cp;
}

} // anonymous namespace

std::u16string utf8_to_utf16(const std::string& input) {
    std::u16string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size();)
        encode_utf16(result, decode_utf8_codepoint(input, i));
    return result;
}

std::string utf16_to_utf8(const std::u16string& input) {
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size();)
        encode_utf8(result, decode_utf16_surrogate(input.data(), input.size(), i));
    return result;
}

std::wstring utf8_to_utf16_w(const std::string& input) {
    // On POSIX, wchar_t is typically 32-bit (UTF-32) — decode to code points directly
    std::wstring result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size();)
        result.push_back(static_cast<wchar_t>(decode_utf8_codepoint(input, i)));
    return result;
}

std::string utf16_to_utf8(const std::wstring& input) {
    // On POSIX, wchar_t is UTF-32 — encode each code point directly
    std::string result;
    result.reserve(input.size());
    for (const auto wc : input)
        encode_utf8(result, static_cast<uint32_t>(wc));
    return result;
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
