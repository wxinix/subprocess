#include "utf8_to_utf16.h"

#ifdef _WIN32
#include <memory>
#include <string_view>
#endif

namespace subprocess
{

#ifndef _WIN32
std::u16string utf8_to_utf16(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::u16string dest = convert.from_bytes(str);
    return dest;
}

std::string utf16_to_utf8(const std::u16string& str)
{
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::string dest = convert.to_bytes(str);
    return dest;
}

std::wstring utf8_to_utf16_w(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert;
    std::wstring dest = convert.from_bytes(str);
    return dest;
}

std::string utf16_to_utf8(const std::wstring& str)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert;
    std::string dest = convert.to_bytes(str);
    return dest;
}
#endif

#ifdef _WIN32

std::u16string utf8_to_utf16(const std::string& input)
{
    std::u16string result{};

    try
    {
        if (!input.empty())
        {
            auto size = input.size() + 1U;
            auto buffer = std::make_unique<wchar_t[]>(size);

            int n = MultiByteToWideChar(static_cast<UINT>(CP_UTF8),               //
                                        static_cast<DWORD>(MB_ERR_INVALID_CHARS), //
                                        input.c_str(),                            //
                                        static_cast<int>(size),                   // Size in bytes of the source
                                        buffer.get(),                             //
                                        static_cast<int>(size));                  // Size in wide chars

            if (n > 0)
            {
                (void)result.assign(buffer.get(), buffer.get() + n - 1);
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(static_cast<UINT>(ERROR_NOT_ENOUGH_MEMORY));
    }

    return result;
}

#ifdef __MINGW32__
// mingw doesn't define this
constexpr int WC_ERR_INVALID_CHARS = 0;
#endif

std::string utf16_to_utf8(const std::u16string& input)
{
    std::string result{};

    try
    {
        if (!input.empty())
        {
            auto size = input.size() + 1U;
            int r = WideCharToMultiByte(static_cast<UINT>(CP_UTF8),                      //
                                        static_cast<DWORD>(WC_ERR_INVALID_CHARS),        //
                                        reinterpret_cast<const wchar_t*>(input.c_str()), //
                                        static_cast<int>(size),                          //
                                        nullptr,                                         //
                                        0,                                               //
                                        nullptr,                                         //
                                        nullptr);

            if (r > 0)
            {
                auto buffer = std::make_unique<char[]>(static_cast<size_t>(r));
                int m = WideCharToMultiByte(static_cast<UINT>(CP_UTF8),                      //
                                            0U,                                              //
                                            reinterpret_cast<const wchar_t*>(input.c_str()), //
                                            static_cast<int>(size),                          //
                                            buffer.get(),                                    //
                                            r,                                               //
                                            nullptr,                                         //
                                            nullptr);

                if (m > 0)
                {
                    (void)result.assign(buffer.get(), buffer.get() + m - 1);
                }
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(static_cast<UINT>(ERROR_NOT_ENOUGH_MEMORY));
    }

    return result;
}

std::string utf16_to_utf8(const std::wstring& input)
{
    return utf16_to_utf8(std::u16string{reinterpret_cast<const char16_t*>(input.c_str()), input.size()});
}

[[maybe_unused]] size_t strlen16(char16_t* input)
{
    size_t size = 0U;
    for (; 0U != *input; ++input)
    {
        ++size;
    }

    return size;
}

size_t strlen16(wchar_t* input)
{
    size_t size = 0U;
    for (; 0U != *input; ++input)
    {
        ++size;
    }

    return size;
}

template <typename T>
    requires std::is_pointer_v<T> && (std::is_same_v<T, char*> || std::is_same_v<T, wchar_t*>)
std::string lptstr_to_string_t(T input)
{
    std::string result;

    if (input == nullptr)
    {
        result = "";
    }
    else if constexpr (sizeof(*input) == 1U)
    {
        result = reinterpret_cast<const char*>(input);
    }
    else
    {
        result = utf16_to_utf8(reinterpret_cast<const wchar_t*>(input));
    }

    return result;
}

std::string lptstr_to_string(LPTSTR input)
{
    return lptstr_to_string_t(input);
}
#endif

} // namespace subprocess