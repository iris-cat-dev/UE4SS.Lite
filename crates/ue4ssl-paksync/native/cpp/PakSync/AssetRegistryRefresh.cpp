#include "PakSync/AssetRegistryRefresh.hpp"

#include "PakSync/AssetPackages.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FAssetData.hpp>
#include <Unreal/UAssetRegistry.hpp>
#include <Unreal/UAssetRegistryHelpers.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace RC::PakSync
{
    namespace
    {
        auto sanitized_pak_stem(const std::wstring& pak_path) -> std::wstring
        {
            auto value = std::filesystem::path{pak_path}.stem().wstring();
            for (auto& ch : value)
            {
                if (ch == L'\\' || ch == L'/' || ch == L'.' || std::iswspace(ch))
                {
                    ch = L'_';
                }
            }
            while (!value.empty() && value.front() == L'_')
            {
                value.erase(value.begin());
            }
            while (!value.empty() && value.back() == L'_')
            {
                value.pop_back();
            }
            return value;
        }

        void add_asset_registry_scan_path(std::vector<std::wstring>& paths, std::wstring path)
        {
            if (path.empty())
            {
                return;
            }
            while (path.size() > 1 && path.back() == L'/')
            {
                path.pop_back();
            }
            const auto already_added = std::any_of(paths.begin(), paths.end(), [&](const std::wstring& existing) {
                return existing == path;
            });
            if (!already_added)
            {
                paths.push_back(std::move(path));
            }
        }
    }

    auto find_asset_registry_objects() -> std::vector<Unreal::UObject*>
    {
        std::vector<Unreal::UObject*> registries{};
        Unreal::UObjectGlobals::FindAllInstancesOfClass(STR("AssetRegistryImpl"), registries);
        if (registries.empty())
        {
            Unreal::UObjectGlobals::FindAllInstancesOfClass(STR("AssetRegistry"), registries);
        }

        if (registries.empty())
        {
            const auto registry_interface = Unreal::UAssetRegistryHelpers::GetAssetRegistry();
            if (registry_interface.ObjectPointer)
            {
                registries.push_back(static_cast<Unreal::UObject*>(registry_interface.ObjectPointer));
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] AssetRegistry fallback via UAssetRegistryHelpers -> 0x{:016X}\n"),
                        reinterpret_cast<uintptr_t>(registry_interface.ObjectPointer));
            }
        }
        return registries;
    }

    auto asset_data_package_name(Unreal::FAssetData& asset_data) -> std::wstring
    {
        auto package_name = asset_data.PackageName().ToString();
        if (!package_name.empty())
        {
            return package_name;
        }

        auto object_path = asset_data.ObjectPath().ToString();
        const auto dot = object_path.find(L'.');
        if (dot != std::wstring::npos)
        {
            object_path.resize(dot);
        }
        return object_path;
    }

    auto asset_data_object_path(Unreal::FAssetData& asset_data) -> std::wstring
    {
        auto object_path = asset_data.ObjectPath().ToString();
        if (!object_path.empty())
        {
            return object_path;
        }

        const auto package_name = asset_data.PackageName().ToString();
        const auto asset_name = asset_data.AssetName().ToString();
        if (!package_name.empty() && !asset_name.empty())
        {
            return package_name + L"." + asset_name;
        }
        return {};
    }

    auto collect_asset_registry_package_snapshot() -> std::optional<std::unordered_set<std::wstring>>
    {
        const auto registries = find_asset_registry_objects();
        for (auto* registry : registries)
        {
            if (!registry)
            {
                continue;
            }

            auto* asset_registry = static_cast<Unreal::UAssetRegistry*>(registry);
            Unreal::TArray<Unreal::FAssetData> all_assets{nullptr, 0, 0};
            if (!asset_registry->GetAllAssets(all_assets, false))
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] AssetRegistry GetAllAssets failed on {}\n"),
                        registry->GetFullName());
                continue;
            }

            std::unordered_set<std::wstring> package_names{};
            package_names.reserve(static_cast<size_t>(std::max<int32_t>(all_assets.Num(), 0)));
            for (int32_t index = 0; index < all_assets.Num(); ++index)
            {
                auto package_name = asset_data_package_name(all_assets[index]);
                if (!package_name.empty())
                {
                    package_names.insert(std::move(package_name));
                }
            }

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] AssetRegistry snapshot packages={} source={}\n"),
                    package_names.size(),
                    registry->GetFullName());
            return package_names;
        }

        return std::nullopt;
    }

    auto asset_registry_scan_paths_for_pak(
            const std::wstring& pak_path,
            const std::vector<std::wstring>& pak_file_packages,
            bool full_game_scan) -> std::vector<std::wstring>
    {
        std::vector<std::wstring> paths{};
        const auto stem = sanitized_pak_stem(pak_path);
        if (!stem.empty())
        {
            add_asset_registry_scan_path(paths, L"/Game/Mods/" + stem);
            add_asset_registry_scan_path(paths, L"/Game/UGC/" + stem);
            add_asset_registry_scan_path(paths, L"/Game/UserGeneratedContent/" + stem);
            add_asset_registry_scan_path(paths, L"/Game/" + stem);
        }

        for (const auto& package_name : pak_file_packages)
        {
            const auto slash = package_name.find_last_of(L'/');
            if (slash != std::wstring::npos && slash > 0)
            {
                add_asset_registry_scan_path(paths, package_name.substr(0, slash));
            }
        }

        if (paths.empty() || full_game_scan)
        {
            add_asset_registry_scan_path(paths, L"/Game");
        }
        return paths;
    }

    auto asset_registry_prefix_paths_for_pak(
            const std::wstring& pak_path,
            const std::vector<std::wstring>& pak_file_packages,
            bool full_game_scan) -> std::vector<std::wstring>
    {
        auto paths = asset_registry_scan_paths_for_pak(pak_path, pak_file_packages, full_game_scan);
        paths.erase(
                std::remove(paths.begin(), paths.end(), std::wstring{L"/Game"}),
                paths.end());
        return paths;
    }

    auto select_asset_registry_packages_for_pak(
            const std::wstring& pak_path,
            const std::vector<std::wstring>& pak_file_packages,
            const std::unordered_set<std::wstring>& after_packages,
            const std::unordered_set<std::wstring>* before_packages) -> AssetRegistryPackageSelection
    {
        AssetRegistryPackageSelection selection{};
        if (!pak_file_packages.empty())
        {
            for (const auto& package_name : pak_file_packages)
            {
                add_unique_package_name(selection.selected_packages, package_name);
            }
        }

        if (selection.selected_packages.empty())
        {
            if (before_packages)
            {
                for (const auto& package_name : after_packages)
                {
                    if (before_packages->find(package_name) == before_packages->end())
                    {
                        add_unique_package_name(selection.diff_packages, package_name);
                    }
                }
            }

            const auto prefix_paths = asset_registry_prefix_paths_for_pak(pak_path, pak_file_packages, false);
            for (const auto& package_name : after_packages)
            {
                for (const auto& prefix_path : prefix_paths)
                {
                    if (package_name_is_under_path(package_name, prefix_path))
                    {
                        add_unique_package_name(selection.prefix_packages, package_name);
                        break;
                    }
                }
            }

            if (selection.diff_packages.size() <= MaxSynthesizedUgcAssetPackages)
            {
                for (const auto& package_name : selection.diff_packages)
                {
                    add_unique_package_name(selection.selected_packages, package_name);
                }
            }
            else
            {
                selection.diff_too_large = true;
            }

            for (const auto& package_name : selection.prefix_packages)
            {
                add_unique_package_name(selection.selected_packages, package_name);
            }
        }

        selection.selected_package_count_before_trim = selection.selected_packages.size();
        if (selection.selected_packages.size() > MaxSynthesizedUgcAssetPackages)
        {
            selection.trimmed = true;
            selection.selected_packages.resize(MaxSynthesizedUgcAssetPackages);
        }

        std::sort(selection.selected_packages.begin(), selection.selected_packages.end());
        return selection;
    }
}
