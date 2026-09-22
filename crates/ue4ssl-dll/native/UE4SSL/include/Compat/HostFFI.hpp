#pragma once

#include <Compat/GeneratedHostAbi.hpp>
#include <Compat/GeneratedRustCoreAbi.hpp>

extern "C"
{
    auto ue4ssl_host_plan_reinstall(uint8_t is_unreal_initialized, uint8_t is_program_started) -> RC::Compat::Host::ReinstallPlan;
    auto ue4ssl_host_plan_reinstall_sequence(uint8_t is_unreal_initialized, uint8_t is_program_started)
            -> RC::Compat::Host::ReinstallSequence;
    auto ue4ssl_host_plan_mod_startup_sequence() -> RC::Compat::Host::ModStartupSequence;
    auto ue4ssl_host_plan_unreal_config(int64_t sig_scanner_num_threads,
                                        int64_t sig_scanner_multithreading_module_size_threshold,
                                        int64_t engine_version_major,
                                        int64_t engine_version_minor,
                                        int64_t fexec_vtable_offset_in_local_player)
            -> RC::Compat::Host::UnrealConfigPlan;
    auto ue4ssl_host_should_dispatch_dll_load(RC::Compat::RustCore::SliceU16 dll_name) -> uint8_t;
    auto ue4ssl_host_reset_dll_dispatch_cache() -> void;
}
