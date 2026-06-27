#include "PakSync/Config.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace RC::PakSync
{
    namespace
    {
        auto read_text_file(const fs::path& path) -> std::optional<std::wstring>
        {
            std::wifstream file{path};
            if (!file)
            {
                return std::nullopt;
            }
            return std::wstring{std::istreambuf_iterator<wchar_t>{file}, std::istreambuf_iterator<wchar_t>{}};
        }

        auto read_bool_config(const std::wstring& contents, const wchar_t* key, bool default_value) -> bool
        {
            const std::wstring needle = std::wstring{key} + L"=";
            const auto pos = contents.find(needle);
            if (pos == std::wstring::npos)
            {
                return default_value;
            }
            const auto start = pos + needle.size();
            const auto end = contents.find_first_of(L"\r\n", start);
            auto value = contents.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            return value == L"true" || value == L"1" || value == L"yes" || value == L"on";
        }

        auto read_u32_config(const std::wstring& contents, const wchar_t* key, uint32_t default_value) -> uint32_t
        {
            const std::wstring needle = std::wstring{key} + L"=";
            const auto pos = contents.find(needle);
            if (pos == std::wstring::npos)
            {
                return default_value;
            }
            const auto start = pos + needle.size();
            const auto end = contents.find_first_of(L"\r\n", start);
            auto value = contents.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            try
            {
                return static_cast<uint32_t>(std::stoul(value));
            }
            catch (...)
            {
                return default_value;
            }
        }
    }

    auto load_config(const fs::path& mod_dir) -> RuntimeConfig
    {
        RuntimeConfig config{};
        const auto config_path = mod_dir / L"config" / L"paksync.ini";
        if (auto contents = read_text_file(config_path))
        {
            config.dry_run = read_bool_config(*contents, L"dry_run", config.dry_run);
            config.enable_detours = read_bool_config(*contents, L"enable_detours", config.enable_detours);
            config.block_travel_until_ready = read_bool_config(*contents, L"block_travel_until_ready", config.block_travel_until_ready);
            config.auto_mount_verified_paks = read_bool_config(*contents, L"auto_mount_verified_paks", config.auto_mount_verified_paks);
            config.stage_synced_paks_for_restart = read_bool_config(*contents, L"stage_synced_paks_for_restart", config.stage_synced_paks_for_restart);
            config.prompt_restart_after_stage = read_bool_config(*contents, L"prompt_restart_after_stage", config.prompt_restart_after_stage);
            config.enable_legacy_hot_refresh = read_bool_config(*contents, L"enable_legacy_hot_refresh", config.enable_legacy_hot_refresh);
            config.auto_ugc_refresh = read_bool_config(*contents, L"auto_ugc_refresh", config.auto_ugc_refresh);
            config.auto_ugc_mount_package = read_bool_config(*contents, L"auto_ugc_mount_package", config.auto_ugc_mount_package);
            config.auto_game_cache_refresh = read_bool_config(*contents, L"auto_game_cache_refresh", config.auto_game_cache_refresh);
            config.asset_registry_full_game_scan = read_bool_config(*contents, L"asset_registry_full_game_scan", config.asset_registry_full_game_scan);
            config.dump_vtables = read_bool_config(*contents, L"dump_vtables", config.dump_vtables);
            config.chunk_size = read_u32_config(*contents, L"chunk_size", config.chunk_size);
            config.send_window = read_u32_config(*contents, L"send_window", config.send_window);
            config.pak_order = read_u32_config(*contents, L"pak_order", config.pak_order);
            config.vtable_entries = read_u32_config(*contents, L"vtable_entries", config.vtable_entries);
            config.host_manifest_timeout_ms = read_u32_config(*contents, L"host_manifest_timeout_ms", config.host_manifest_timeout_ms);
        }
        config.chunk_size = std::clamp<uint32_t>(config.chunk_size, 1024, 64 * 1024);
        config.send_window = std::clamp<uint32_t>(config.send_window, 1, 64);
        config.pak_order = std::clamp<uint32_t>(config.pak_order, 0, 100000);
        config.vtable_entries = std::clamp<uint32_t>(config.vtable_entries, 8, 256);
        if (config.host_manifest_timeout_ms != 0)
        {
            config.host_manifest_timeout_ms = std::clamp<uint32_t>(config.host_manifest_timeout_ms, 5000, 60000);
        }
        return config;
    }
}
