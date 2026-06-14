#include <Compat/HostShim.hpp>

#include <Compat/HostFFI.hpp>

namespace
{
    static_assert(sizeof(RC::CharType) == sizeof(uint16_t));

    auto make_slice(RC::StringViewType value) -> RC::Compat::RustCore::SliceU16
    {
        return {
                reinterpret_cast<const uint16_t*>(value.data()),
                value.size(),
        };
    }
}

namespace RC::Compat::HostApi
{
    auto plan_reinstall(bool is_unreal_initialized, bool is_program_started) -> Host::ReinstallPlan
    {
        return ue4ssl_host_plan_reinstall(is_unreal_initialized ? 1 : 0, is_program_started ? 1 : 0);
    }

    auto plan_reinstall_sequence(bool is_unreal_initialized, bool is_program_started) -> Host::ReinstallSequence
    {
        return ue4ssl_host_plan_reinstall_sequence(is_unreal_initialized ? 1 : 0, is_program_started ? 1 : 0);
    }

    auto plan_mod_startup_sequence() -> Host::ModStartupSequence
    {
        return ue4ssl_host_plan_mod_startup_sequence();
    }

    auto plan_unreal_config(int64_t sig_scanner_num_threads,
                            int64_t sig_scanner_multithreading_module_size_threshold,
                            int64_t engine_version_major,
                            int64_t engine_version_minor,
                            int64_t fexec_vtable_offset_in_local_player) -> Host::UnrealConfigPlan
    {
        return ue4ssl_host_plan_unreal_config(sig_scanner_num_threads,
                                             sig_scanner_multithreading_module_size_threshold,
                                             engine_version_major,
                                             engine_version_minor,
                                             fexec_vtable_offset_in_local_player);
    }

    auto should_dispatch_dll_load(StringViewType dll_name) -> bool
    {
        return ue4ssl_host_should_dispatch_dll_load(make_slice(dll_name)) != 0;
    }

    auto reset_dll_dispatch_cache() -> void
    {
        ue4ssl_host_reset_dll_dispatch_cache();
    }
} // namespace RC::Compat::HostApi
