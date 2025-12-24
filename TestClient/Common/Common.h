#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
#include <windows.h>

namespace Common::Encoding
{
    // wstring(UTF-16) -> string(UTF-8)
    [[nodiscard]] inline std::string WideToUtf8(std::wstring_view wsrc)
    {
        if (wsrc.empty())
            return {};

        int bytes = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wsrc.data(),
            static_cast<int>(wsrc.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        if (bytes <= 0) {
            throw std::runtime_error("WideCharToMultiByte sizing failed, error=" + std::to_string(GetLastError()));
        }

        std::string out;
        out.resize(bytes);

        int written = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wsrc.data(),
            static_cast<int>(wsrc.size()),
            out.data(),
            bytes,
            nullptr,
            nullptr
        );
        if (written <= 0) {
            throw std::runtime_error("WideCharToMultiByte conversion failed, error=" + std::to_string(GetLastError()));
        }

        if (!out.empty() && out.back() == '\0') {
            out.pop_back();
        }
        return out;
    }

    // string(UTF-8) -> wstring(UTF-16)
    [[nodiscard]] inline std::wstring Utf8ToWide(std::string_view src)
    {
        if (src.empty())
            return {};

        int wchars = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            src.data(),
            static_cast<int>(src.size()),
            nullptr,
            0
        );

        if (wchars <= 0) {
            throw std::runtime_error("MultiByteToWideChar sizing failed, error=" + std::to_string(GetLastError()));
        }

        std::wstring out;
        out.resize(wchars);

        int written = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            src.data(),
            static_cast<int>(src.size()),
            out.data(),
            wchars
        );

        if (written <= 0) {
            throw std::runtime_error("MultiByteToWideChar conversion failed, error=" + std::to_string(GetLastError()));
        }
        return out;
    }
}