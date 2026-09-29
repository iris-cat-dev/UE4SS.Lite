#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace RC::PakSync
{
    constexpr size_t MaxSynthesizedUgcAssetPackages = 4096;

    auto package_name_is_under_path(const std::wstring& package_name, const std::wstring& path) -> bool;
    auto add_unique_package_name(std::vector<std::wstring>& package_names, const std::wstring& package_name) -> bool;
    auto add_unique_wstring(std::vector<std::wstring>& values, const std::wstring& value) -> bool;
    auto lowercase_wstring(std::wstring value) -> std::wstring;
    auto normalize_raw_asset_filename_to_package(std::string_view raw_path) -> std::wstring;
    auto normalize_raw_game_package_string(std::string_view raw_path) -> std::wstring;
    auto package_name_leaf(const std::wstring& package_name) -> std::wstring;
    auto looks_like_map_package_name(const std::wstring& package_name) -> bool;
    auto build_ugc_asset_reference_variants(const std::vector<std::wstring>& package_names) -> std::vector<std::wstring>;
    auto extract_asset_package_names_from_pak_file(const std::filesystem::path& pak) -> std::vector<std::wstring>;
}
