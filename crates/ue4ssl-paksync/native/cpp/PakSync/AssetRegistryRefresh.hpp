#pragma once

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace RC::Unreal
{
    struct FAssetData;
    class UObject;
}

namespace RC::PakSync
{
    struct AssetRegistryPackageSelection
    {
        std::vector<std::wstring> selected_packages{};
        std::vector<std::wstring> diff_packages{};
        std::vector<std::wstring> prefix_packages{};
        size_t selected_package_count_before_trim{};
        bool diff_too_large{};
        bool trimmed{};
    };

    auto find_asset_registry_objects() -> std::vector<Unreal::UObject*>;
    auto asset_data_package_name(Unreal::FAssetData& asset_data) -> std::wstring;
    auto asset_data_object_path(Unreal::FAssetData& asset_data) -> std::wstring;
    auto collect_asset_registry_package_snapshot() -> std::optional<std::unordered_set<std::wstring>>;
    auto asset_registry_scan_paths_for_pak(
            const std::wstring& pak_path,
            const std::vector<std::wstring>& pak_file_packages,
            bool full_game_scan) -> std::vector<std::wstring>;
    auto asset_registry_prefix_paths_for_pak(
            const std::wstring& pak_path,
            const std::vector<std::wstring>& pak_file_packages,
            bool full_game_scan) -> std::vector<std::wstring>;
    auto select_asset_registry_packages_for_pak(
            const std::wstring& pak_path,
            const std::vector<std::wstring>& pak_file_packages,
            const std::unordered_set<std::wstring>& after_packages,
            const std::unordered_set<std::wstring>* before_packages) -> AssetRegistryPackageSelection;
}
