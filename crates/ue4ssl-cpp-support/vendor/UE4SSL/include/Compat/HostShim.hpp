#pragma once

#include <Compat/GeneratedHostAbi.hpp>
#include <String/StringType.hpp>

namespace RC::Compat::HostApi
{
    auto plan_reinstall(bool is_unreal_initialized, bool is_program_started) -> Host::ReinstallPlan;
    auto plan_reinstall_sequence(bool is_unreal_initialized, bool is_program_started) -> Host::ReinstallSequence;
    auto plan_mod_startup_sequence() -> Host::ModStartupSequence;
    auto plan_unreal_config(int64_t sig_scanner_num_threads,
                            int64_t sig_scanner_multithreading_module_size_threshold,
                            int64_t engine_version_major,
                            int64_t engine_version_minor,
                            int64_t fexec_vtable_offset_in_local_player) -> Host::UnrealConfigPlan;
    auto should_dispatch_dll_load(StringViewType dll_name) -> bool;
    auto reset_dll_dispatch_cache() -> void;
}
