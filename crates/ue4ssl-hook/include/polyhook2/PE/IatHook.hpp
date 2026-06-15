#pragma once

#include <cstdint>
#include <string>

#include <UE4SSHook/ffi.hpp>
#include <polyhook2/Misc.hpp>

namespace PLH
{
    class IatHook
    {
    public:
        IatHook(const std::string& dll_name,
                const std::string& api_name,
                const char* fn_callback,
                uint64_t* user_orig_var,
                const std::wstring& module_name)
            : IatHook(dll_name, api_name, reinterpret_cast<uint64_t>(fn_callback), user_orig_var, module_name)
        {
        }

        IatHook(const std::string& dll_name,
                const std::string& api_name,
                uint64_t fn_callback,
                uint64_t* user_orig_var,
                const std::wstring& module_name)
            : m_handle(ue4ss_iat_hook_create(
                  dll_name.c_str(),
                  api_name.c_str(),
                  fn_callback,
                  user_orig_var,
                  module_name.c_str()))
        {
        }

        IatHook(const IatHook&) = delete;
        IatHook(IatHook&&) = delete;
        auto operator=(const IatHook&) -> IatHook& = delete;
        auto operator=(IatHook&&) -> IatHook& = delete;

        ~IatHook()
        {
            ue4ss_iat_hook_destroy(m_handle);
        }

        auto hook() -> bool
        {
            return m_handle ? ue4ss_iat_hook_hook(m_handle) : false;
        }

        auto unHook() -> bool
        {
            return m_handle ? ue4ss_iat_hook_unhook(m_handle) : false;
        }

        auto isHooked() const -> bool
        {
            return m_handle ? ue4ss_iat_hook_is_hooked(m_handle) : false;
        }

    private:
        Ue4ssHookIatHookHandle* m_handle{};
    };
}
