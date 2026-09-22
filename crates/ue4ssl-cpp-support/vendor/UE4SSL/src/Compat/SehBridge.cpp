#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <exception>
#include <Compat/SehBridge.hpp>
#include <Mod/CppUserModBase.hpp>
#include <SehFramework.hpp>

namespace
{
    // C++ exceptions terminate here, before returning through a Rust frame.
    // SEH belongs to the outer native leaf, not Rust catch_unwind.
    template <typename F>
    void invoke_cpp(const char* operation, F callable) noexcept
    {
        try { callable(); }
        catch (const std::exception& error) { std::fprintf(stderr, "[UE4SSL] %s: %s\n", operation, error.what()); }
        catch (...) { std::fprintf(stderr, "[UE4SSL] %s: unknown C++ exception\n", operation); }
    }
    void* start_cpp(void* fn) noexcept
    {
        void* result = nullptr;
        invoke_cpp("start_mod", [&] { result = reinterpret_cast<RC::CppUserModBase* (*)()>(fn)(); });
        return result;
    }
    void uninstall_cpp(void* fn, void* mod) noexcept
    {
        invoke_cpp("uninstall_mod", [&] { reinterpret_cast<void (*)(RC::CppUserModBase*)>(fn)(static_cast<RC::CppUserModBase*>(mod)); });
    }
    void unreal_cpp(void* mod) noexcept { invoke_cpp("on_unreal_init", [&] { static_cast<RC::CppUserModBase*>(mod)->on_unreal_init(); }); }
    void ui_cpp(void* mod) noexcept { invoke_cpp("on_ui_init", [&] { static_cast<RC::CppUserModBase*>(mod)->on_ui_init(); }); }
    void program_cpp(void* mod) noexcept { invoke_cpp("on_program_start", [&] { static_cast<RC::CppUserModBase*>(mod)->on_program_start(); }); }
    void update_cpp(void* mod) noexcept { invoke_cpp("on_update", [&] { static_cast<RC::CppUserModBase*>(mod)->on_update(); }); }
    void dll_cpp(void* mod, const uint16_t* name, size_t len) noexcept
    {
        invoke_cpp("on_dll_load", [&] {
            static_cast<RC::CppUserModBase*>(mod)->on_dll_load({reinterpret_cast<const RC::CharType*>(name), len});
        });
    }
    void free_cpp(void* module) noexcept { invoke_cpp("FreeLibrary", [&] { FreeLibrary(static_cast<HMODULE>(module)); }); }
}

extern "C"
{
    void* ue4ssl_native_cppmod_call_start(void* fn)
    {
        __try { return start_cpp(fn); }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"start_mod", GetExceptionCode(), GetExceptionInformation())) { return nullptr; }
    }
    void ue4ssl_native_cppmod_call_uninstall(void* fn, void* mod)
    {
        __try { uninstall_cpp(fn, mod); }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"uninstall_mod", GetExceptionCode(), GetExceptionInformation())) {}
    }
    void ue4ssl_native_cppmod_call_on_unreal_init(void* mod)
    {
        __try { unreal_cpp(mod); }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"on_unreal_init", GetExceptionCode(), GetExceptionInformation())) {}
    }
    void ue4ssl_native_cppmod_call_on_ui_init(void* mod)
    {
        __try { ui_cpp(mod); }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"on_ui_init", GetExceptionCode(), GetExceptionInformation())) {}
    }
    void ue4ssl_native_cppmod_call_on_program_start(void* mod)
    {
        __try { program_cpp(mod); }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"on_program_start", GetExceptionCode(), GetExceptionInformation())) {}
    }
    void ue4ssl_native_cppmod_call_on_update(void* mod)
    {
        __try { update_cpp(mod); }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"on_update", GetExceptionCode(), GetExceptionInformation())) {}
    }
    void ue4ssl_native_cppmod_call_on_dll_load(void* mod, const uint16_t* name, size_t len)
    {
        __try { dll_cpp(mod, name, len); }
        __except (RC::Seh::FilterAndLog(L"CppMod", L"on_dll_load", GetExceptionCode(), GetExceptionInformation())) {}
    }
    void ue4ssl_native_cppmod_free_library(void* module)
    {
        __try { free_cpp(module); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}
