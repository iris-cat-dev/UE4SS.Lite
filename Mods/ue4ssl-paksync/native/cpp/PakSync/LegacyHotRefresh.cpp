#define NOMINMAX

#include "PakSync/LegacyHotRefresh.hpp"

#include "PakSync/AssetPackages.hpp"
#include "PakSync/AssetRegistryRefresh.hpp"
#include "PakSync/Config.hpp"
#include "PakSync/DrgUgc.hpp"
#include "PakSync/Resolver.hpp"
#include "PakSync/RuntimeUtils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <Windows.h>
#include <DynamicOutput/DynamicOutput.hpp>
#include <SehFramework.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FAssetData.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/UAssetRegistry.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace fs = std::filesystem;

namespace RC::PakSync
{
namespace
{
void log_reflected_function_params(Unreal::UObject* object, const CharType* function_name)
{
    if (!object)
    {
        return;
    }

    auto* function = object->GetFunctionByNameInChain(function_name);
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] reflected function {} on {} -> 0x{:016X}\n"),
            function_name,
            object->GetFullName(),
            reinterpret_cast<uintptr_t>(function));
    if (!function)
    {
        return;
    }

    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] function {} parms_size={}\n"),
            function->GetFullName(),
            function->GetParmsSize());
    for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function,
                 Unreal::EFieldIterationFlags::IncludeDeprecated))
    {
        if (!prop || !prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm))
        {
            continue;
        }

        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync]   param {}\n"),
                prop->GetName());
    }
}

bool invoke_no_param_function(Unreal::UObject* object, const CharType* function_name)
{
    if (!object)
    {
        return false;
    }

    auto* function = object->GetFunctionByNameInChain(function_name);
    if (!function)
    {
        return false;
    }
    if (function->GetParmsSize() != 0)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] skipped {} on {} because parms_size={} expected 0\n"),
                function_name,
                object->GetFullName(),
                function->GetParmsSize());
        return false;
    }

    std::array<uint8_t, 1> params{};
    const auto invoke_start = GetTickCount64();
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] invoking {} on {} start\n"),
            function_name,
            object->GetFullName());
    if (!Seh::SafeProcessEvent(object, function, params.data()))
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] {} failed during SafeProcessEvent on {}\n"),
                function_name,
                object->GetFullName());
        return false;
    }
    const auto invoke_elapsed = GetTickCount64() - invoke_start;
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] invoked {} on {} elapsed_ms={}\n"),
            function_name,
            object->GetFullName(),
            invoke_elapsed);
    return true;
}

bool invoke_apply_pending_mods(Unreal::UObject* subsystem, bool from_joining, bool from_start_screen)
{
    if (!subsystem)
    {
        return false;
    }

    auto* function = subsystem->GetFunctionByNameInChain(STR("ApplyPendingMods"));
    if (!function)
    {
        return false;
    }

    const int32_t params_size = function->GetParmsSize();
    if (params_size != 2)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] ApplyPendingMods skipped: unexpected parms_size={} expected=2\n"),
                params_size);
        return false;
    }

    std::vector<uint8_t> params(static_cast<size_t>(params_size), 0);
    bool wrote_from_joining = false;
    bool wrote_from_start_screen = false;
    uint32_t reflected_param_count = 0;
    bool unexpected_param = false;
    for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function,
                 Unreal::EFieldIterationFlags::IncludeDeprecated))
    {
        if (!prop || !prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm))
        {
            continue;
        }

        ++reflected_param_count;
        const auto prop_name = prop->GetName();
        const bool size_matches = prop->GetSize() == sizeof(bool);
        if (prop_name == STR("FromJoining") && size_matches)
        {
            *prop->ContainerPtrToValuePtr<bool>(params.data()) = from_joining;
            wrote_from_joining = true;
        }
        else if (prop_name == STR("FromStartScreen") && size_matches)
        {
            *prop->ContainerPtrToValuePtr<bool>(params.data()) = from_start_screen;
            wrote_from_start_screen = true;
        }
        else
        {
            unexpected_param = true;
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] ApplyPendingMods unexpected param {} size={}\n"),
                    prop_name,
                    prop->GetSize());
        }
    }

    if (!wrote_from_joining || !wrote_from_start_screen || reflected_param_count != 2 || unexpected_param)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] ApplyPendingMods skipped: params FromJoining={} FromStartScreen={} count={} unexpected={}\n"),
                wrote_from_joining,
                wrote_from_start_screen,
                reflected_param_count,
                unexpected_param);
        return false;
    }

    subsystem->ProcessEvent(function, params.data());
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] invoked ApplyPendingMods on {} FromJoining={} FromStartScreen={}\n"),
            subsystem->GetFullName(),
            from_joining,
            from_start_screen);
    return true;
}

}

bool LegacyHotRefresh::enabled(const RuntimeConfig& config) const
{
    return config.enable_legacy_hot_refresh || (!config.stage_synced_paks_for_restart && config.auto_mount_verified_paks);
}

bool LegacyHotRefresh::idle(const RuntimeConfig& config) const
{
    return !enabled(config) || (m_pending_asset_registry_scans.empty() && !m_post_mount_refresh_in_progress);
}

bool LegacyHotRefresh::has_pending_scans() const { return !m_pending_asset_registry_scans.empty(); }
bool LegacyHotRefresh::post_mount_refresh_in_progress() const { return m_post_mount_refresh_in_progress; }
bool LegacyHotRefresh::game_cache_refresh_pending() const { return m_game_cache_refresh_pending_this_join; }
bool LegacyHotRefresh::partial_hot_reload_detected() const { return m_partial_hot_reload_detected; }
bool LegacyHotRefresh::game_cache_refresh_attempted() const { return m_game_cache_refresh_attempted_this_join; }
bool LegacyHotRefresh::game_cache_refresh_pending_or_waiting_or_post_seen() const
{
    return m_game_cache_refresh_pending_this_join || m_game_cache_refresh_waiting_for_load_post || m_load_map_post_seen_this_join;
}
bool LegacyHotRefresh::cache_waiting() const { return m_game_cache_refresh_waiting_for_load_post; }
bool LegacyHotRefresh::cache_pending() const { return m_game_cache_refresh_pending_this_join; }
size_t LegacyHotRefresh::pending_scan_count() const { return m_pending_asset_registry_scans.size(); }

void LegacyHotRefresh::reset_for_join(bool keep_post_mount_refresh)
{
    m_pending_asset_registry_scans.clear();
    m_post_mount_refresh_in_progress = false;
    if (!keep_post_mount_refresh)
    {
        m_game_cache_refresh_pending_this_join = false;
        m_game_cache_refresh_waiting_for_load_post = false;
        m_game_cache_refresh_attempted_this_join = false;
        m_game_cache_refresh_missing_logged = false;
        m_game_cache_refresh_not_before_ms = 0;
        m_load_map_post_seen_this_join = false;
    }
}

void LegacyHotRefresh::mark_load_map_post_seen(size_t mounted_count)
{
    m_load_map_post_seen_this_join = true;
    if (!m_game_cache_refresh_waiting_for_load_post ||
        m_game_cache_refresh_pending_this_join ||
        m_game_cache_refresh_attempted_this_join)
    {
        return;
    }
    m_game_cache_refresh_waiting_for_load_post = false;
    m_game_cache_refresh_pending_this_join = true;
    m_game_cache_refresh_not_before_ms = GetTickCount64();
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] scheduled post-load generic game cache refresh mounted={} delay_ms=0 partial={}\n"),
            mounted_count,
            m_partial_hot_reload_detected);
}

void LegacyHotRefresh::run_update(
        const RuntimeConfig& config,
        const FunctionTable& functions,
        const std::vector<fs::path>& pending_mounts,
        const std::vector<fs::path>& mounted_paks)
{
    if (!enabled(config))
    {
        return;
    }
    if (!m_pending_asset_registry_scans.empty())
    {
        retry_pending_asset_registry_scans(config, functions, pending_mounts, mounted_paks);
    }
    run_deferred_game_cache_refresh_if_ready(config, pending_mounts, mounted_paks);
}

auto LegacyHotRefresh::collect_before_mount(const RuntimeConfig& config) -> std::optional<std::unordered_set<std::wstring>>
{
    return enabled(config) ? collect_asset_registry_package_snapshot() : std::optional<std::unordered_set<std::wstring>>{};
}

void LegacyHotRefresh::after_pak_mounted(
        const RuntimeConfig& config,
        const FunctionTable& functions,
        const fs::path& pak,
        const std::unordered_set<std::wstring>* before_packages,
        const std::vector<fs::path>& pending_mounts,
        const std::vector<fs::path>& mounted_paks)
{
    if (!enabled(config))
    {
        return;
    }

    const auto pak_path = pak.wstring();
    if (refresh_asset_registry_after_mount(config, functions, pak_path))
    {
        cache_asset_registry_packages_for_pak(config, pak, before_packages);
        m_asset_registry_packages_before_mount.erase(pak_path);
        finish_post_mount_refresh(config, pak);
    }
    else
    {
        if (before_packages)
        {
            m_asset_registry_packages_before_mount[pak_path] = *before_packages;
        }
        queue_asset_registry_scan(config, pak);
    }
    schedule_game_cache_refresh_after_all_mounts(config, pending_mounts, mounted_paks);
}

bool LegacyHotRefresh::refresh_asset_registry_after_mount(
        const RuntimeConfig& config,
        const FunctionTable& functions,
        const std::wstring& pak_path)
{
    if (!m_mount_point_skip_logged &&
        (!functions.f_package_name_register_mount_point.address ||
         functions.f_package_name_register_mount_point.confidence < 80 ||
         functions.f_package_name_register_mount_point.match_count != 1))
    {
        m_mount_point_skip_logged = true;
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] mount point registration skipped: resolver confidence={} matches={}\n"),
                functions.f_package_name_register_mount_point.confidence,
                functions.f_package_name_register_mount_point.match_count);
    }

    const auto registries = find_asset_registry_objects();
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] AssetRegistry candidates={} after mount pak={}\n"),
            registries.size(),
            pak_path);

    for (auto* registry : registries)
    {
        if (!registry)
        {
            continue;
        }

        auto* function = registry->GetFunctionByNameInChain(STR("ScanPathsSynchronous"));
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] AssetRegistry candidate {} ScanPathsSynchronous=0x{:016X}\n"),
                registry->GetFullName(),
                reinterpret_cast<uintptr_t>(function));
        if (!function)
        {
            continue;
        }

        const int32_t params_size = function->GetParmsSize();
        if (params_size <= 0 || params_size > 1024)
        {
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] AssetRegistry scan skipped: unexpected params size={} function={}\n"),
                    params_size,
                    function->GetFullName());
            continue;
        }

        std::vector<uint8_t> params(static_cast<size_t>(params_size), 0);
        const auto scan_paths = PakSync::asset_registry_scan_paths_for_pak(
                pak_path,
                pak_file_asset_packages_for_pak(fs::path{pak_path}),
                config.asset_registry_full_game_scan);
        Unreal::TArray<Unreal::FString>* constructed_paths = nullptr;
        for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                     function,
                     Unreal::EFieldIterationFlags::IncludeDeprecated))
        {
            if (!prop || !prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm))
            {
                continue;
            }

            const auto prop_name = prop->GetName();
            if (prop_name == STR("InPaths"))
            {
                auto* paths = prop->ContainerPtrToValuePtr<Unreal::TArray<Unreal::FString>>(params.data());
                new (paths) Unreal::TArray<Unreal::FString>();
                for (const auto& scan_path : scan_paths)
                {
                    paths->Add(Unreal::FString(scan_path.c_str()));
                }
                constructed_paths = paths;
            }
            else if (prop_name == STR("bForceRescan"))
            {
                *prop->ContainerPtrToValuePtr<bool>(params.data()) = true;
            }
        }

        if (!constructed_paths)
        {
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] AssetRegistry scan skipped: InPaths parameter not found on {}\n"),
                    function->GetFullName());
            continue;
        }

        const auto scan_start = GetTickCount64();
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] scanning AssetRegistry {} path(s) after pak mount start\n"),
                constructed_paths->Num());
        for (const auto& scan_path : scan_paths)
        {
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] AssetRegistry scan path {}\n"),
                    scan_path);
        }
        registry->ProcessEvent(function, params.data());
        const auto scan_elapsed = GetTickCount64() - scan_start;
        constructed_paths->~TArray<Unreal::FString>();
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] AssetRegistry ScanPathsSynchronous completed elapsed_ms={}\n"),
                scan_elapsed);
        return true;
    }
    return false;
}

auto LegacyHotRefresh::pak_file_asset_packages_for_pak(const fs::path& pak) -> std::vector<std::wstring>
{
    const auto key = pak.wstring();
    const auto found = m_pak_file_asset_packages_by_pak.find(key);
    if (found != m_pak_file_asset_packages_by_pak.end())
    {
        return found->second;
    }

    auto package_names = extract_asset_package_names_from_pak_file(pak);
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] pak file asset extraction pak={} packages={}\n"),
            key,
            package_names.size());
    for (size_t index = 0; index < std::min<size_t>(package_names.size(), 16); ++index)
    {
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync]   pak asset package {}\n"),
                package_names[index]);
    }
    m_pak_file_asset_packages_by_pak[key] = std::move(package_names);
    return m_pak_file_asset_packages_by_pak[key];
}

void LegacyHotRefresh::detect_preexisting_packages_for_pak(
        const RuntimeConfig& config,
        const fs::path& pak,
        const std::unordered_set<std::wstring>* before_packages)
{
    if (!enabled(config))
    {
        return;
    }
    if (!before_packages)
    {
        return;
    }

    const auto pak_path = pak.wstring();
    std::vector<std::wstring> preexisting_packages{};

    for (const auto& package_name : pak_file_asset_packages_for_pak(pak))
    {
        if (before_packages->find(package_name) != before_packages->end())
        {
            add_unique_package_name(preexisting_packages, package_name);
        }
    }

    if (preexisting_packages.empty())
    {
        m_preloaded_package_conflicts_by_pak.erase(pak_path);
        return;
    }

    m_partial_hot_reload_detected = true;
    m_preloaded_package_conflicts_by_pak[pak_path] = preexisting_packages;
    Output::send<LogLevel::Warning>(
            STR("[UE4SSL.PakSync] pak contains {} package(s) already known before mount; generic refresh may not replace loaded or cached game state pak={}\n"),
            preexisting_packages.size(),
            pak_path);
    for (size_t index = 0; index < std::min<size_t>(preexisting_packages.size(), 16); ++index)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync]   preexisting package {}\n"),
                preexisting_packages[index]);
    }
}

void LegacyHotRefresh::cache_asset_registry_packages_for_pak(
        const RuntimeConfig& config,
        const fs::path& pak,
        const std::unordered_set<std::wstring>* before_packages)
{
    const auto pak_path = pak.wstring();
    const auto after_packages = collect_asset_registry_package_snapshot();
    if (!after_packages)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC asset package cache skipped: AssetRegistry snapshot unavailable pak={}\n"),
                pak_path);
        return;
    }

    auto pak_file_packages = pak_file_asset_packages_for_pak(pak);
            auto selection = select_asset_registry_packages_for_pak(
                    pak_path,
                    pak_file_packages,
                    *after_packages,
                    before_packages);
    if (selection.diff_too_large)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC asset package diff too large ({} > {}), ignoring diff and using path-filtered packages only pak={}\n"),
                selection.diff_packages.size(),
                MaxSynthesizedUgcAssetPackages,
                pak_path);
    }
    if (selection.trimmed)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC asset package cache too large ({} > {}), trimming pak={}\n"),
                selection.selected_package_count_before_trim,
                MaxSynthesizedUgcAssetPackages,
                pak_path);
    }

    m_ugc_package_assets_by_pak[pak_path] = std::move(selection.selected_packages);

    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] UGC asset package cache pak={} before={} after={} pak_file={} diff={} prefix={} selected={}\n"),
            pak_path,
            before_packages ? before_packages->size() : 0,
            after_packages->size(),
            pak_file_packages.size(),
            selection.diff_packages.size(),
            selection.prefix_packages.size(),
            m_ugc_package_assets_by_pak[pak_path].size());
    for (size_t index = 0; index < std::min<size_t>(m_ugc_package_assets_by_pak[pak_path].size(), 16); ++index)
    {
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync]   UGC asset package {}\n"),
                m_ugc_package_assets_by_pak[pak_path][index]);
    }

            cache_ugc_resources_for_pak(pak_path);
        }

auto LegacyHotRefresh::cached_ugc_asset_packages_for_pak(const std::wstring& pak_path) const -> std::vector<std::wstring>
{
    const auto found = m_ugc_package_assets_by_pak.find(pak_path);
    if (found == m_ugc_package_assets_by_pak.end())
    {
        return {};
    }
    return found->second;
}

void LegacyHotRefresh::cache_ugc_resources_for_pak(const std::wstring& pak_path)
{
    auto package_names = cached_ugc_asset_packages_for_pak(pak_path);
    if (package_names.empty())
    {
        package_names = pak_file_asset_packages_for_pak(fs::path{pak_path});
    }

    PakUgcResourceCache cache{};
    for (const auto& package_name : package_names)
    {
        add_unique_package_name(cache.package_names, package_name);
    }

    std::unordered_set<std::wstring> package_lookup{};
    package_lookup.reserve(cache.package_names.size());
    for (const auto& package_name : cache.package_names)
    {
        package_lookup.insert(package_name);
    }

    size_t asset_registry_matches = 0;
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
                    STR("[UE4SSL.PakSync] PakSync UGC resource cache skipped AssetRegistry source={} pak={}\n"),
                    registry->GetFullName(),
                    pak_path);
            continue;
        }

        for (int32_t index = 0; index < all_assets.Num(); ++index)
        {
            auto package_name = asset_data_package_name(all_assets[index]);
            if (package_lookup.find(package_name) == package_lookup.end())
            {
                continue;
            }

            ++asset_registry_matches;
            const auto asset_class = all_assets[index].AssetClass().ToString();
            const auto asset_name = all_assets[index].AssetName().ToString();
            const auto object_path = asset_data_object_path(all_assets[index]);
            if (asset_class == L"World")
            {
                add_unique_wstring(
                        cache.map_names,
                        !asset_name.empty() ? asset_name : package_name_leaf(package_name));
            }
            else if (asset_class == L"Blueprint" || asset_class.ends_with(L"Blueprint"))
            {
                if (!object_path.empty())
                {
                    add_unique_wstring(cache.class_object_paths, object_path + L"_C");
                }
            }
        }

        break;
    }

    for (const auto& package_name : cache.package_names)
    {
        const auto leaf = package_name_leaf(package_name);
        if (leaf.empty())
        {
            continue;
        }

        const auto class_path = package_name + L"." + leaf + L"_C";
        add_unique_wstring(cache.class_object_paths, class_path);
        if (looks_like_map_package_name(package_name))
        {
            add_unique_wstring(cache.map_names, leaf);
        }
    }

    for (const auto& class_path : cache.class_object_paths)
    {
        auto* object = Seh::SafeStaticFindObject(class_path);
        if (object)
        {
            add_unique_wstring(cache.visible_class_object_paths, class_path);
        }
    }

    std::sort(cache.package_names.begin(), cache.package_names.end());
    std::sort(cache.class_object_paths.begin(), cache.class_object_paths.end());
    std::sort(cache.visible_class_object_paths.begin(), cache.visible_class_object_paths.end());
    std::sort(cache.map_names.begin(), cache.map_names.end());

    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] PakSync UGC resource cache pak={} packages={} asset_registry_matches={} class_candidates={} visible_classes={} maps={}\n"),
            pak_path,
            cache.package_names.size(),
            asset_registry_matches,
            cache.class_object_paths.size(),
            cache.visible_class_object_paths.size(),
            cache.map_names.size());
    for (size_t index = 0; index < std::min<size_t>(cache.class_object_paths.size(), 12); ++index)
    {
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync]   PakSync class_candidate[{}]={}\n"),
                index,
                cache.class_object_paths[index]);
    }
    for (size_t index = 0; index < std::min<size_t>(cache.visible_class_object_paths.size(), 12); ++index)
    {
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync]   PakSync visible_class[{}]={}\n"),
                index,
                cache.visible_class_object_paths[index]);
    }
    for (size_t index = 0; index < std::min<size_t>(cache.map_names.size(), 12); ++index)
    {
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync]   PakSync map_candidate[{}]={}\n"),
                index,
                cache.map_names[index]);
    }

    m_ugc_resource_cache_by_pak[pak_path] = std::move(cache);
}

void LegacyHotRefresh::log_paksync_ugc_resource_cache_for_pak(const std::wstring& pak_path) const
{
    const auto found = m_ugc_resource_cache_by_pak.find(pak_path);
    if (found == m_ugc_resource_cache_by_pak.end())
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] PakSync UGC resource cache unavailable for pak={}\n"),
                pak_path);
        return;
    }

    const auto& cache = found->second;
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] PakSync UGC resource cache summary pak={} packages={} class_candidates={} visible_classes={} maps={}\n"),
            pak_path,
            cache.package_names.size(),
            cache.class_object_paths.size(),
            cache.visible_class_object_paths.size(),
            cache.map_names.size());
}

void LegacyHotRefresh::refresh_drg_after_pak_mount(const RuntimeConfig& config, const std::wstring& pak_path)
{
    if (!config.auto_ugc_refresh)
    {
        return;
    }

    const auto package_names = cached_ugc_asset_packages_for_pak(pak_path);
    if (package_names.empty())
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC package-backed refresh has no AssetRegistry package names for pak={}; ApplyPendingMods fallback will still run\n"),
                pak_path);
    }

    std::vector<Unreal::UObject*> registries{};
    Unreal::UObjectGlobals::FindAllInstancesOfClass(STR("UGCRegistry"), registries);
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] UGCRegistry candidates={} after mount pak={}\n"),
            registries.size(),
            pak_path);

    Unreal::UObject* selected_registry = nullptr;
    for (auto* registry : registries)
    {
        if (!registry)
        {
            continue;
        }

        log_reflected_function_params(registry, STR("MountUGCPackage"));
        log_reflected_function_params(registry, STR("RegisterAssetFromPackage"));
        log_reflected_function_params(registry, STR("ResetUGCPackagesManipulatedDuringJoin"));

        if (!registry->GetFunctionByNameInChain(STR("RegisterAssetFromPackage")))
        {
            continue;
        }

        const auto full_name = registry->GetFullName();
        if (!selected_registry || full_name.find(STR("UGCRegistry_Windows")) != decltype(full_name)::npos)
        {
            selected_registry = registry;
        }
    }

    Unreal::UObject* synthesized_package = nullptr;
    if (selected_registry)
    {
        (void)invoke_no_param_function(selected_registry, STR("ResetUGCPackagesManipulatedDuringJoin"));
        if (!package_names.empty())
        {
            synthesized_package = find_existing_ugc_package_for_pak(selected_registry, pak_path, package_names);
            const bool using_existing_package = synthesized_package != nullptr;
            if (!synthesized_package)
            {
                synthesized_package = create_synthesized_ugc_package(selected_registry, pak_path, package_names);
            }
            if (synthesized_package)
            {
                log_synthesized_ugc_package_identity(synthesized_package);
                if (using_existing_package)
                {
                    Output::send<LogLevel::Normal>(
                            STR("[UE4SSL.PakSync] MountUGCPackage skipped: reusing existing UGC package package={}\n"),
                            synthesized_package->GetFullName());
                }
                else if (config.auto_ugc_mount_package && is_safe_for_native_ugc_mount(synthesized_package))
                {
                    const bool mount_ok = invoke_mount_ugc_package(
                            selected_registry,
                            synthesized_package,
                            true);
                    if (!mount_ok)
                    {
                        (void)set_bool_property(synthesized_package, STR("IsMounted"), true);
                        Output::send<LogLevel::Warning>(
                                STR("[UE4SSL.PakSync] MountUGCPackage returned false; falling back to manual UGC package registration package={}\n"),
                                synthesized_package->GetFullName());
                    }
                }
                else if (config.auto_ugc_mount_package)
                {
                    (void)set_bool_property(synthesized_package, STR("IsMounted"), true);
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] MountUGCPackage skipped: synthesized package failed native mount safety check package={}\n"),
                            synthesized_package->GetFullName());
                }
                else
                {
                    (void)set_bool_property(synthesized_package, STR("IsMounted"), true);
                    Output::send<LogLevel::Normal>(
                            STR("[UE4SSL.PakSync] MountUGCPackage skipped by config; using manual UGC package registration package={}\n"),
                            synthesized_package->GetFullName());
                }
                (void)invoke_package_param_function(
                        selected_registry,
                        STR("RegisterAssetFromPackage"),
                        STR("Package"),
                        synthesized_package);
                (void)invoke_get_all_classes_in_package(selected_registry, synthesized_package);
                (void)invoke_get_maps_in_package(selected_registry, synthesized_package);
                log_paksync_ugc_resource_cache_for_pak(pak_path);
            }
        }
    }
    else
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC package-backed refresh skipped: no UGCRegistry with RegisterAssetFromPackage found\n"));
    }

    std::vector<Unreal::UObject*> subsystems{};
    Unreal::UObjectGlobals::FindAllInstancesOfClass(STR("UGCSubsystem"), subsystems);
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] UGCSubsystem candidates={} after mount\n"),
            subsystems.size());
    for (auto* subsystem : subsystems)
    {
        if (!subsystem)
        {
            continue;
        }
        log_reflected_function_params(subsystem, STR("ApplyPendingMods"));
        const auto effective_mod_id = effective_ugc_mod_id(synthesized_package);
        const auto reflected_mod_id = reflected_ugc_mod_id(synthesized_package);
        log_ugc_settings_state(subsystem, synthesized_package, effective_mod_id);
        if (synthesized_package)
        {
            (void)invoke_set_packages_as_recently_installed(subsystem, synthesized_package);
            if (!reflected_mod_id.empty())
            {
                (void)add_mod_id_to_selected_ugc_slot(subsystem, reflected_mod_id);
                (void)invoke_set_mods_as_recently_installed(subsystem, reflected_mod_id);
            }
            else
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] SetModsAsRecentlyInstalled and selected-slot update skipped: synthesized package has no reflected mod id package={} effective_mod_id={}\n"),
                        synthesized_package->GetFullName(),
                        effective_mod_id.empty() ? L"<none>" : effective_mod_id);
            }
        }
        (void)invoke_apply_pending_mods(subsystem, true, false);
        if (synthesized_package)
        {
            (void)invoke_no_param_function(subsystem, STR("MarkRecentlyInstalledModsSuccesful"));
        }
        (void)set_bool_property(subsystem, STR("IsLocalUserModsInstalled"), true);
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] UGCSubsystem state after ApplyPendingMods object={} IsLocalUserModsInstalled={}\n"),
                subsystem->GetFullName(),
                get_bool_property(subsystem, STR("IsLocalUserModsInstalled")).value_or(false));
        log_ugc_settings_state(subsystem, synthesized_package, effective_mod_id);
    }
}

static Unreal::UObject* find_game_instance_for_cache_refresh()
{
    std::vector<Unreal::UObject*> game_instances{};
    Unreal::UObjectGlobals::FindAllInstancesOfClass(STR("FSDGameInstance"), game_instances);
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] FSDGameInstance candidates={} for game cache refresh\n"),
            game_instances.size());
    if (game_instances.empty())
    {
        Unreal::UObjectGlobals::FindAllInstancesOfClass(STR("GameInstance"), game_instances);
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] GameInstance fallback candidates={} for game cache refresh\n"),
                game_instances.size());
    }

    Unreal::UObject* selected_instance = nullptr;
    for (auto* instance : game_instances)
    {
        if (!instance)
        {
            continue;
        }

        log_reflected_function_params(instance, STR("ResetAlwaysLoadedWorldsAndGameData"));
        log_reflected_function_params(instance, STR("RefreshIsGameModded"));

        if (!instance->GetFunctionByNameInChain(STR("RefreshIsGameModded")))
        {
            continue;
        }

        selected_instance = instance;
        break;
    }
    return selected_instance;
}

void LegacyHotRefresh::schedule_game_cache_refresh_after_all_mounts(
        const RuntimeConfig& config,
        const std::vector<fs::path>& pending_mounts,
        const std::vector<fs::path>& mounted_paks)
{
    if (!config.auto_game_cache_refresh || m_game_cache_refresh_attempted_this_join)
    {
        return;
    }
    if (!pending_mounts.empty() || !m_pending_asset_registry_scans.empty())
    {
        return;
    }
    if (mounted_paks.empty())
    {
        return;
    }

    if (m_game_cache_refresh_waiting_for_load_post || m_game_cache_refresh_pending_this_join)
    {
        return;
    }
    m_game_cache_refresh_waiting_for_load_post = true;
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] armed generic game cache refresh for LoadMapPost mounted={} partial={}\n"),
            mounted_paks.size(),
            m_partial_hot_reload_detected);
    if (m_load_map_post_seen_this_join)
    {
        m_game_cache_refresh_waiting_for_load_post = false;
        m_game_cache_refresh_pending_this_join = true;
        m_game_cache_refresh_not_before_ms = GetTickCount64();
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] LoadMapPost already observed; scheduled post-load generic game cache refresh mounted={} delay_ms=0 partial={}\n"),
                        mounted_paks.size(),
                m_partial_hot_reload_detected);
    }
}

void LegacyHotRefresh::run_deferred_game_cache_refresh_if_ready(
        const RuntimeConfig&,
        const std::vector<fs::path>& pending_mounts,
        const std::vector<fs::path>& mounted_paks)
{
    if (!m_game_cache_refresh_pending_this_join)
    {
        return;
    }
    if (!pending_mounts.empty() || !m_pending_asset_registry_scans.empty())
    {
        return;
    }
    const auto now = GetTickCount64();
    if (now < m_game_cache_refresh_not_before_ms)
    {
        return;
    }

    m_game_cache_refresh_pending_this_join = false;
    m_game_cache_refresh_attempted_this_join = true;
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] starting deferred generic game cache refresh after pak mount mounted={}\n"),
            mounted_paks.size());
    const bool was_in_progress = m_post_mount_refresh_in_progress;
    m_post_mount_refresh_in_progress = true;
    auto* game_instance = find_game_instance_for_cache_refresh();
    if (!game_instance)
    {
        m_post_mount_refresh_in_progress = was_in_progress;
        if (!m_game_cache_refresh_missing_logged)
        {
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] generic game cache refresh skipped: no FSDGameInstance with RefreshIsGameModded found\n"));
            m_game_cache_refresh_missing_logged = true;
        }
        return;
    }

    Output::send<LogLevel::Warning>(
            STR("[UE4SSL.PakSync] ResetAlwaysLoadedWorldsAndGameData skipped: verified unsafe in DRG during join/post-load (hang or stack overflow)\n"));
    const bool modded_ok = invoke_no_param_function(game_instance, STR("RefreshIsGameModded"));
    m_post_mount_refresh_in_progress = was_in_progress;
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] generic game cache refresh completed instance={} reset_skipped=true refresh_modded={}\n"),
            game_instance->GetFullName(),
            modded_ok);
}

void LegacyHotRefresh::report_mid_join_partial_state(const std::wstring& pak_path)
{
    const auto conflicts = m_preloaded_package_conflicts_by_pak.find(pak_path);
    if (conflicts != m_preloaded_package_conflicts_by_pak.end() && !conflicts->second.empty())
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] post-mount refresh partial for {}; {} package(s) were loaded before mount and may keep old UObject state\n"),
                pak_path,
                conflicts->second.size());
        return;
    }

    Output::send<LogLevel::Verbose>(
            STR("[UE4SSL.PakSync] post-mount refresh finished for {}; no preloaded package conflicts detected\n"),
            pak_path);
}

void LegacyHotRefresh::finish_post_mount_refresh(const RuntimeConfig& config, const fs::path& pak)
{
    if (!enabled(config))
    {
        return;
    }
    const auto pak_path = pak.wstring();
    refresh_drg_after_pak_mount(config, pak_path);
    report_mid_join_partial_state(pak_path);
}

void LegacyHotRefresh::queue_asset_registry_scan(const RuntimeConfig& config, const fs::path& pak)
{
    if (!enabled(config))
    {
        return;
    }
    const auto already_pending = std::any_of(m_pending_asset_registry_scans.begin(), m_pending_asset_registry_scans.end(), [&](const fs::path& pending) {
        return pending == pak;
    });
    if (!already_pending)
    {
        m_pending_asset_registry_scans.push_back(pak);
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] AssetRegistry not ready after mount; queued retry for {}\n"),
                pak.wstring());
    }
}

void LegacyHotRefresh::retry_pending_asset_registry_scans(
        const RuntimeConfig& config,
        const FunctionTable& functions,
        const std::vector<fs::path>& pending_mounts,
        const std::vector<fs::path>& mounted_paks)
{
    if (!enabled(config))
    {
        m_pending_asset_registry_scans.clear();
        return;
    }
    for (auto it = m_pending_asset_registry_scans.begin(); it != m_pending_asset_registry_scans.end();)
    {
        const auto pak = *it;
        const auto pak_path = pak.wstring();
        const auto now = GetTickCount64();
        if (now < m_next_asset_registry_retry_ms)
        {
            break;
        }
        m_next_asset_registry_retry_ms = now + 2000;

        m_post_mount_refresh_in_progress = true;
        if (refresh_asset_registry_after_mount(config, functions, pak_path))
        {
            const auto before_it = m_asset_registry_packages_before_mount.find(pak_path);
            const auto* before_packages =
                    before_it != m_asset_registry_packages_before_mount.end() ? &before_it->second : nullptr;
            cache_asset_registry_packages_for_pak(config, pak, before_packages);
            if (before_it != m_asset_registry_packages_before_mount.end())
            {
                m_asset_registry_packages_before_mount.erase(before_it);
            }
            finish_post_mount_refresh(config, pak);
            m_post_mount_refresh_in_progress = false;
            it = m_pending_asset_registry_scans.erase(it);
            schedule_game_cache_refresh_after_all_mounts(config, pending_mounts, mounted_paks);
        }
        else
        {
            m_post_mount_refresh_in_progress = false;
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] AssetRegistry still unavailable; keeping pre-travel scan pending for {}\n"),
                    pak_path);
            ++it;
        }
    }
}

}
