#pragma once

#include "PakSync/RuntimeTypes.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace RC::PakSync
{
    struct RuntimeConfig;
    struct FunctionTable;

    class LegacyHotRefresh
    {
    public:
        bool enabled(const RuntimeConfig& config) const;
        bool idle(const RuntimeConfig& config) const;
        bool has_pending_scans() const;
        bool post_mount_refresh_in_progress() const;
        bool game_cache_refresh_pending() const;
        bool partial_hot_reload_detected() const;
        bool game_cache_refresh_attempted() const;
        bool game_cache_refresh_pending_or_waiting_or_post_seen() const;
        bool cache_waiting() const;
        bool cache_pending() const;

        size_t pending_scan_count() const;
        void reset_for_join(bool keep_post_mount_refresh);
        void mark_load_map_post_seen(size_t mounted_count);
        void run_update(
                const RuntimeConfig& config,
                const FunctionTable& functions,
                const std::vector<std::filesystem::path>& pending_mounts,
                const std::vector<std::filesystem::path>& mounted_paks);

        auto collect_before_mount(const RuntimeConfig& config) -> std::optional<std::unordered_set<std::wstring>>;
        void detect_preexisting_packages_for_pak(
                const RuntimeConfig& config,
                const std::filesystem::path& pak,
                const std::unordered_set<std::wstring>* before_packages);
        void after_pak_mounted(
                const RuntimeConfig& config,
                const FunctionTable& functions,
                const std::filesystem::path& pak,
                const std::unordered_set<std::wstring>* before_packages,
                const std::vector<std::filesystem::path>& pending_mounts,
                const std::vector<std::filesystem::path>& mounted_paks);
        void schedule_game_cache_refresh_after_all_mounts(
                const RuntimeConfig& config,
                const std::vector<std::filesystem::path>& pending_mounts,
                const std::vector<std::filesystem::path>& mounted_paks);

    private:
        bool refresh_asset_registry_after_mount(
                const RuntimeConfig& config,
                const FunctionTable& functions,
                const std::wstring& pak_path);
        void queue_asset_registry_scan(const RuntimeConfig& config, const std::filesystem::path& pak);
        void retry_pending_asset_registry_scans(
                const RuntimeConfig& config,
                const FunctionTable& functions,
                const std::vector<std::filesystem::path>& pending_mounts,
                const std::vector<std::filesystem::path>& mounted_paks);
        void run_deferred_game_cache_refresh_if_ready(
                const RuntimeConfig& config,
                const std::vector<std::filesystem::path>& pending_mounts,
                const std::vector<std::filesystem::path>& mounted_paks);
        void finish_post_mount_refresh(const RuntimeConfig& config, const std::filesystem::path& pak);
        void report_mid_join_partial_state(const std::wstring& pak_path);
        void cache_asset_registry_packages_for_pak(
                const RuntimeConfig& config,
                const std::filesystem::path& pak,
                const std::unordered_set<std::wstring>* before_packages);
        void cache_ugc_resources_for_pak(const std::wstring& pak_path);
        void refresh_drg_after_pak_mount(const RuntimeConfig& config, const std::wstring& pak_path);
        void log_paksync_ugc_resource_cache_for_pak(const std::wstring& pak_path) const;
        auto pak_file_asset_packages_for_pak(const std::filesystem::path& pak) -> std::vector<std::wstring>;
        auto cached_ugc_asset_packages_for_pak(const std::wstring& pak_path) const -> std::vector<std::wstring>;

        std::vector<std::filesystem::path> m_pending_asset_registry_scans{};
        std::unordered_map<std::wstring, std::unordered_set<std::wstring>> m_asset_registry_packages_before_mount{};
        std::unordered_map<std::wstring, std::vector<std::wstring>> m_pak_file_asset_packages_by_pak{};
        std::unordered_map<std::wstring, std::vector<std::wstring>> m_ugc_package_assets_by_pak{};
        std::unordered_map<std::wstring, PakUgcResourceCache> m_ugc_resource_cache_by_pak{};
        std::unordered_map<std::wstring, std::vector<std::wstring>> m_preloaded_package_conflicts_by_pak{};
        uint64_t m_next_asset_registry_retry_ms{};
        uint64_t m_game_cache_refresh_not_before_ms{};
        bool m_partial_hot_reload_detected{};
        bool m_post_mount_refresh_in_progress{};
        bool m_game_cache_refresh_pending_this_join{};
        bool m_game_cache_refresh_waiting_for_load_post{};
        bool m_game_cache_refresh_attempted_this_join{};
        bool m_game_cache_refresh_missing_logged{};
        bool m_load_map_post_seen_this_join{};
        bool m_mount_point_skip_logged{};
    };
}
