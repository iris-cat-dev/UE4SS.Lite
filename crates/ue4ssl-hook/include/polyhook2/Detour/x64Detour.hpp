#pragma once

#include <cstdint>

#include <UE4SSHook/ffi.hpp>
#include <polyhook2/Misc.hpp>

namespace PLH
{
    class x64Detour
    {
    public:
        enum detour_scheme_t : uint8_t
        {
            VALLOC2 = 1 << 0,
            INPLACE = 1 << 1,
            CODE_CAVE = 1 << 2,
            INPLACE_SHORT = 1 << 3,
            RECOMMENDED = VALLOC2 | INPLACE | CODE_CAVE,
            ALL = RECOMMENDED | INPLACE_SHORT,
        };

        x64Detour(uint64_t fn_address, uint64_t fn_callback, uint64_t* user_tramp_var)
            : m_handle(ue4ss_detour_create(fn_address, fn_callback, user_tramp_var))
        {
        }

        x64Detour(const x64Detour&) = delete;
        x64Detour(x64Detour&&) = delete;
        auto operator=(const x64Detour&) -> x64Detour& = delete;
        auto operator=(x64Detour&&) -> x64Detour& = delete;

        ~x64Detour()
        {
            ue4ss_detour_destroy(m_handle);
        }

        auto hook() -> bool
        {
            return m_handle ? ue4ss_detour_hook(m_handle) : false;
        }

        auto unHook() -> bool
        {
            return m_handle ? ue4ss_detour_unhook(m_handle) : false;
        }

        auto isHooked() const -> bool
        {
            return m_handle ? ue4ss_detour_is_hooked(m_handle) : false;
        }

        auto getDetourScheme() const -> detour_scheme_t
        {
            return m_scheme;
        }

        auto setDetourScheme(detour_scheme_t scheme) -> void
        {
            m_scheme = scheme;
            if (m_handle)
            {
                ue4ss_detour_set_scheme(m_handle, static_cast<uint32_t>(scheme));
            }
        }

    private:
        Ue4ssHookDetourHandle* m_handle{};
        detour_scheme_t m_scheme{RECOMMENDED};
    };
}
