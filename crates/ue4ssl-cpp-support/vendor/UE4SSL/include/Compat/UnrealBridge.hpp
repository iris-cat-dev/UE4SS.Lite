#pragma once

#include <cstdint>

namespace RC::Compat::UnrealBridge
{
    struct RuntimeState
    {
        uint8_t is_unreal_initialized{};
        uint8_t is_versioned_container_initialized{};
        uint8_t is_pre_init_completed{};
        uint8_t is_scan_fully_completed{};
    };

    auto setup_unreal_modules() -> void;
    auto runtime_state() -> RuntimeState;
    auto is_unreal_initialized() -> bool;
    auto set_max_asset_loading_memory(int64_t max_memory_usage_during_asset_loading) -> void;
} // namespace RC::Compat::UnrealBridge
