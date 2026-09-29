#pragma once

#include <cstdint>
#include <filesystem>

namespace RC::PakSync
{
    constexpr uint32_t DefaultChunkSize = 16 * 1024;

    struct RuntimeConfig
    {
        bool dry_run{true};
        bool enable_detours{false};
        bool block_travel_until_ready{false};
        bool auto_mount_verified_paks{false};
        bool stage_synced_paks_for_restart{false};
        bool prompt_restart_after_stage{true};
        bool enable_legacy_hot_refresh{false};
        bool auto_ugc_refresh{true};
        bool auto_ugc_mount_package{false};
        bool auto_game_cache_refresh{false};
        bool asset_registry_full_game_scan{false};
        bool dump_vtables{true};
        uint32_t chunk_size{DefaultChunkSize};
        uint32_t send_window{2};
        uint32_t pak_order{1000};
        uint32_t vtable_entries{96};
        uint32_t host_manifest_timeout_ms{30000};
    };

    auto load_config(const std::filesystem::path& mod_dir) -> RuntimeConfig;
}
