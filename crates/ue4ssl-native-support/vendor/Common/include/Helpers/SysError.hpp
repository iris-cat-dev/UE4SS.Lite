#pragma once

#include <String/StringType.hpp>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace RC
{
    /// Convert a system error code (e.g. from GetLastError()) to a string.
    inline StringType SysError(unsigned long err)
    {
#ifdef _WIN32
        wchar_t* buf = nullptr;
        if (FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                err,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<LPWSTR>(&buf),
                0,
                nullptr) != 0 && buf)
        {
            StringType result(buf);
            LocalFree(buf);
            return result;
        }
        return StringType(L"Unknown error ") + std::to_wstring(err);
#else
        (void)err;
        return StringType(L"SysError not implemented on this platform");
#endif
    }
}
