#pragma once

#include <Compat/GeneratedHostAbi.hpp>
#include <String/StringType.hpp>

namespace RC::Compat::HostApi
{
    auto plan_unreal_config(int64_t sig_scanner_num_threads,
                            int64_t sig_scanner_multithreading_module_size_threshold,
                            int64_t engine_version_major,
                            int64_t engine_version_minor,
                            int64_t fexec_vtable_offset_in_local_player) -> Host::UnrealConfigPlan;
}
