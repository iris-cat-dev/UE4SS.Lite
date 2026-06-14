#define NOMINMAX

#include <Windows.h>

#include <Compat/SehBridge.hpp>
#include <Mod/CppUserModBase.hpp>
#include <SehFramework.hpp>

extern "C"
{
    auto ue4ssl_native_cppmod_call_start(void* fn) -> void*
    {
        __try
        {
            auto start_mod = reinterpret_cast<RC::CppUserModBase* (*)()>(fn);
            return start_mod();
        }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"start_mod", GetExceptionCode(), GetExceptionInformation()))
        {
            return nullptr;
        }
    }

    auto ue4ssl_native_cppmod_call_uninstall(void* fn, void* mod) -> void
    {
        __try
        {
            auto uninstall_mod = reinterpret_cast<void (*)(RC::CppUserModBase*)>(fn);
            uninstall_mod(static_cast<RC::CppUserModBase*>(mod));
        }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"uninstall_mod", GetExceptionCode(), GetExceptionInformation()))
        {
        }
    }

    auto ue4ssl_native_cppmod_call_on_unreal_init(void* mod) -> void
    {
        __try
        {
            static_cast<RC::CppUserModBase*>(mod)->on_unreal_init();
        }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"on_unreal_init", GetExceptionCode(), GetExceptionInformation()))
        {
        }
    }

    auto ue4ssl_native_cppmod_call_on_ui_init(void* mod) -> void
    {
        __try
        {
            static_cast<RC::CppUserModBase*>(mod)->on_ui_init();
        }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"on_ui_init", GetExceptionCode(), GetExceptionInformation()))
        {
        }
    }

    auto ue4ssl_native_cppmod_call_on_program_start(void* mod) -> void
    {
        __try
        {
            static_cast<RC::CppUserModBase*>(mod)->on_program_start();
        }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"on_program_start", GetExceptionCode(), GetExceptionInformation()))
        {
        }
    }

    auto ue4ssl_native_cppmod_call_on_update(void* mod) -> void
    {
        __try
        {
            static_cast<RC::CppUserModBase*>(mod)->on_update();
        }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"on_update", GetExceptionCode(), GetExceptionInformation()))
        {
        }
    }

    auto ue4ssl_native_cppmod_call_on_dll_load(void* mod, const uint16_t* dll_name, size_t dll_name_len) -> void
    {
        __try
        {
            RC::StringViewType dll_name_view{reinterpret_cast<const RC::CharType*>(dll_name), dll_name_len};
            static_cast<RC::CppUserModBase*>(mod)->on_dll_load(dll_name_view);
        }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"on_dll_load", GetExceptionCode(), GetExceptionInformation()))
        {
        }
    }

    auto ue4ssl_native_cppmod_free_library(void* module_handle) -> void
    {
        __try
        {
            FreeLibrary(static_cast<HMODULE>(module_handle));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
}
