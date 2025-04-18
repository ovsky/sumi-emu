// SPDX-FileCopyrightText: 2013 Dolphin Emulator Project
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 Sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cctype>
#include <codecvt>
#include <locale>
#include <sstream>

#include "common/string_util.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef ANDROID
#include <common/fs/fs_android.h>
#endif

namespace Common {

/// Make a string lowercase
std::string ToLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

/// Make a string uppercase
std::string ToUpper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return str;
}

std::string StringFromBuffer(std::span<const u8> data) {
    return std::string(data.begin(), std::find(data.begin(), data.end(), '\0'));
}

std::string StringFromBuffer(std::span<const char> data) {
    return std::string(data.begin(), std::find(data.begin(), data.end(), '\0'));
}

// Turns "  hej " into "hej". Also handles tabs.
std::string StripSpaces(const std::string& str) {
    const std::size_t s = str.find_first_not_of(" \t\r\n");

    if (str.npos != s)
        return str.substr(s, str.find_last_not_of(" \t\r\n") - s + 1);
    else
        return "";
}

// "\"hello\"" is turned to "hello"
// This one assumes that the string has already been space stripped in both
// ends, as done by StripSpaces above, for example.
std::string StripQuotes(const std::string& s) {
    if (s.size() && '\"' == s[0] && '\"' == *s.rbegin())
        return s.substr(1, s.size() - 2);
    else
        return s;
}

std::string StringFromBool(bool value) {
    return value ? "True" : "False";
}

bool SplitPath(const std::string& full_path, std::string* _pPath, std::string* _pFilename,
               std::string* _pExtension) {
    if (full_path.empty())
        return false;

#ifdef ANDROID
    if (full_path[0] != '/') {
        *_pPath = Common::FS::Android::GetParentDirectory(full_path);
        *_pFilename = Common::FS::Android::GetFilename(full_path);
        return true;
    }
#endif

    std::size_t dir_end = full_path.find_last_of("/"
// windows needs the : included for something like just "C:" to be considered a directory
#ifdef _WIN32
                                                 "\\:"
#endif
    );
    if (std::string::npos == dir_end)
        dir_end = 0;
    else
        dir_end += 1;

    std::size_t fname_end = full_path.rfind('.');
    if (fname_end < dir_end || std::string::npos == fname_end)
        fname_end = full_path.size();

    if (_pPath)
        *_pPath = full_path.substr(0, dir_end);

    if (_pFilename)
        *_pFilename = full_path.substr(dir_end, fname_end - dir_end);

    if (_pExtension)
        *_pExtension = full_path.substr(fname_end);

    return true;
}

void SplitString(const std::string& str, const char delim, std::vector<std::string>& output) {
    std::istringstream iss(str);
    output.resize(1);

    while (std::getline(iss, *output.rbegin(), delim)) {
        output.emplace_back();
    }

    output.pop_back();
}

std::string TabsToSpaces(int tab_size, std::string in) {
    std::size_t i = 0;

    while ((i = in.find('\t')) != std::string::npos) {
        in.replace(i, 1, tab_size, ' ');
    }

    return in;
}

std::string ReplaceAll(std::string result, const std::string& src, const std::string& dest) {
    std::size_t pos = 0;

    if (src == dest)
        return result;

    while ((pos = result.find(src, pos)) != std::string::npos) {
        result.replace(pos, src.size(), dest);
        pos += dest.length();
    }

    return result;
}

std::string UTF16ToUTF8(std::u16string_view input) {
#ifdef _WIN32
    return UTF16ToUTF8(std::wstring_view{reinterpret_cast<const wchar_t*>(input.data()), input.size()});
#else
    std::string result;
    result.reserve(input.size() * 3); // UTF-8 can use up to 3 bytes per UTF-16 character

    for (size_t i = 0; i < input.size(); ) {
        char32_t code_point;

        // Handle surrogate pairs
        if (i + 1 < input.size() &&
            (input[i] & 0xFC00) == 0xD800 &&
            (input[i + 1] & 0xFC00) == 0xDC00) {
            // Surrogate pair
            code_point = 0x10000;
            code_point += (input[i] & 0x3FF) << 10;
            code_point += (input[i + 1] & 0x3FF);
            i += 2;
        } else {
            code_point = input[i];
            i++;
        }

        // Convert to UTF-8
        if (code_point < 0x80) {
            result += static_cast<char>(code_point);
        } else if (code_point < 0x800) {
            result += static_cast<char>((code_point >> 6) | 0xC0);
            result += static_cast<char>((code_point & 0x3F) | 0x80);
        } else if (code_point < 0x10000) {
            result += static_cast<char>((code_point >> 12) | 0xE0);
            result += static_cast<char>(((code_point >> 6) & 0x3F) | 0x80);
            result += static_cast<char>((code_point & 0x3F) | 0x80);
        } else {
            result += static_cast<char>((code_point >> 18) | 0xF0);
            result += static_cast<char>(((code_point >> 12) & 0x3F) | 0x80);
            result += static_cast<char>(((code_point >> 6) & 0x3F) | 0x80);
            result += static_cast<char>((code_point & 0x3F) | 0x80);
        }
    }
    return result;
#endif
}

std::u16string UTF8ToUTF16(std::string_view input) {
#ifdef _WIN32
    const auto wide = UTF8ToUTF16W(input);
    return std::u16string{reinterpret_cast<const char16_t*>(wide.data()), wide.size()};
#else
    std::u16string result;
    result.reserve(input.size()); // Reserve at least the same size

    for (size_t i = 0; i < input.size(); ) {
        char32_t code_point = 0;
        unsigned char byte = input[i];

        if (byte < 0x80) {
            code_point = byte;
            i += 1;
        } else if ((byte & 0xE0) == 0xC0) {
            if (i + 1 >= input.size()) break;
            code_point = ((byte & 0x1F) << 6) | (input[i + 1] & 0x3F);
            i += 2;
        } else if ((byte & 0xF0) == 0xE0) {
            if (i + 2 >= input.size()) break;
            code_point = ((byte & 0x0F) << 12) |
                        ((input[i + 1] & 0x3F) << 6) |
                        (input[i + 2] & 0x3F);
            i += 3;
        } else if ((byte & 0xF8) == 0xF0) {
            if (i + 3 >= input.size()) break;
            code_point = ((byte & 0x07) << 18) |
                        ((input[i + 1] & 0x3F) << 12) |
                        ((input[i + 2] & 0x3F) << 6) |
                        (input[i + 3] & 0x3F);
            i += 4;
        } else {
            i += 1;
            continue;
        }

        if (code_point <= 0xFFFF) {
            result += static_cast<char16_t>(code_point);
        } else {
            // Surrogate pair encoding
            code_point -= 0x10000;
            result += static_cast<char16_t>(0xD800 + (code_point >> 10));
            result += static_cast<char16_t>(0xDC00 + (code_point & 0x3FF));
        }
    }
    return result;
#endif
}

std::u32string UTF8ToUTF32(std::string_view input) {
    std::u32string result;
    result.reserve(input.size()); // Reserve at least the same size

    for (size_t i = 0; i < input.size(); ) {
        char32_t code_point = 0;
        unsigned char byte = input[i];

        if (byte < 0x80) {
            code_point = byte;
            i += 1;
        } else if ((byte & 0xE0) == 0xC0) {
            if (i + 1 >= input.size()) break;
            code_point = ((byte & 0x1F) << 6) | (input[i + 1] & 0x3F);
            i += 2;
        } else if ((byte & 0xF0) == 0xE0) {
            if (i + 2 >= input.size()) break;
            code_point = ((byte & 0x0F) << 12) |
                        ((input[i + 1] & 0x3F) << 6) |
                        (input[i + 2] & 0x3F);
            i += 3;
        } else if ((byte & 0xF8) == 0xF0) {
            if (i + 3 >= input.size()) break;
            code_point = ((byte & 0x07) << 18) |
                        ((input[i + 1] & 0x3F) << 12) |
                        ((input[i + 2] & 0x3F) << 6) |
                        (input[i + 3] & 0x3F);
            i += 4;
        } else {
            i += 1;
            continue;
        }

        result += code_point;
    }
    return result;
}

#ifdef _WIN32
static std::wstring CPToUTF16(u32 code_page, std::string_view input) {
    const auto size =
        MultiByteToWideChar(code_page, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);

    if (size == 0) {
        return {};
    }

    std::wstring output(size, L'\0');

    if (size != MultiByteToWideChar(code_page, 0, input.data(), static_cast<int>(input.size()),
                                    &output[0], static_cast<int>(output.size()))) {
        output.clear();
    }

    return output;
}

std::string UTF16ToUTF8(std::wstring_view input) {
    const auto size = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        return {};
    }

    std::string output(size, '\0');

    if (size != WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                                    &output[0], static_cast<int>(output.size()), nullptr,
                                    nullptr)) {
        output.clear();
    }

    return output;
}

std::wstring UTF8ToUTF16W(std::string_view input) {
    return CPToUTF16(CP_UTF8, input);
}

#endif

std::u16string U16StringFromBuffer(const u16* input, std::size_t length) {
    return std::u16string(reinterpret_cast<const char16_t*>(input), length);
}

std::string StringFromFixedZeroTerminatedBuffer(std::string_view buffer, std::size_t max_len) {
    std::size_t len = 0;
    while (len < buffer.length() && len < max_len && buffer[len] != '\0') {
        ++len;
    }
    return std::string(buffer.begin(), buffer.begin() + len);
}

std::u16string UTF16StringFromFixedZeroTerminatedBuffer(std::u16string_view buffer,
                                                        std::size_t max_len) {
    std::size_t len = 0;
    while (len < buffer.length() && len < max_len && buffer[len] != '\0') {
        ++len;
    }
    return std::u16string(buffer.begin(), buffer.begin() + len);
}

} // namespace Common
