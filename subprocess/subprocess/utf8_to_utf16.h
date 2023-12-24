#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace subprocess
{
/**
 * @brief Convert UTF-8 encoded string to UTF-16 encoded string.
 * @param input The input UTF-8 string.
 * @return UTF-16 encoded string.
 */
std::u16string utf8_to_utf16(const std::string& input);

/**
 * @brief Convert UTF-16 encoded string to UTF-8 encoded string.
 * @param input The input UTF-16 string.
 * @return UTF-8 encoded string.
 */
std::string utf16_to_utf8(const std::u16string& input);

/**
 * @brief Convert UTF-8 encoded string to UTF-16 encoded wide string.
 * @param input The input UTF-8 string.
 * @return UTF-16 encoded wide string.
 */
std::wstring utf8_to_utf16_w(const std::string& input);

/**
 * @brief Convert UTF-16 encoded wide string to UTF-8 encoded string.
 * @param input The input UTF-16 wide string.
 * @return UTF-8 encoded string.
 */
std::string utf16_to_utf8(const std::wstring& input);

/**
 * @brief Calculate the length of a UTF-16 encoded string (char16_t array).
 * @param input The input UTF-16 string.
 * @return Length of the UTF-16 string.
 */
[[maybe_unused]] size_t strlen16(char16_t* input);

/**
 * @brief Calculate the length of a UTF-16 encoded wide string (wchar_t array).
 * @param input The input UTF-16 wide string.
 * @return Length of the UTF-16 wide string.
 */
size_t strlen16(wchar_t* input);

#ifdef _WIN32
/**
 * @brief Convert a Windows LPTSTR string to a UTF-8 encoded string.
 * @param input The input LPTSTR string.
 * @return UTF-8 encoded string.
 */
std::string lptstr_to_string(LPTSTR input);
#endif

} // namespace subprocess