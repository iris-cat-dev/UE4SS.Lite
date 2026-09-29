#pragma once

#include <optional>
#include <string>
#include <vector>

#include <String/StringType.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace RC::PakSync
{
    std::optional<bool> get_bool_property(Unreal::UObject* object, const CharType* property_name);
    bool set_bool_property(Unreal::UObject* object, const CharType* property_name, bool value);

    Unreal::UObject* find_existing_ugc_package_for_pak(
            Unreal::UObject* registry,
            const std::wstring& pak_path,
            const std::vector<std::wstring>& package_names);
    Unreal::UObject* create_synthesized_ugc_package(
            Unreal::UObject* registry,
            const std::wstring& pak_path,
            const std::vector<std::wstring>& package_names);

    bool invoke_package_param_function(
            Unreal::UObject* object,
            const CharType* function_name,
            const CharType* package_param_name,
            Unreal::UObject* package);
    bool invoke_mount_ugc_package(Unreal::UObject* registry, Unreal::UObject* package, bool from_joining);
    bool invoke_get_all_classes_in_package(Unreal::UObject* registry, Unreal::UObject* package);
    bool invoke_get_maps_in_package(Unreal::UObject* registry, Unreal::UObject* package);
    bool invoke_set_packages_as_recently_installed(Unreal::UObject* subsystem, Unreal::UObject* package);
    bool invoke_set_mods_as_recently_installed(Unreal::UObject* subsystem, const std::wstring& mod_id);

    void log_synthesized_ugc_package_identity(Unreal::UObject* package);
    std::wstring effective_ugc_mod_id(Unreal::UObject* package);
    std::wstring reflected_ugc_mod_id(Unreal::UObject* package);
    bool is_safe_for_native_ugc_mount(Unreal::UObject* package);
    bool add_mod_id_to_selected_ugc_slot(Unreal::UObject* subsystem, const std::wstring& mod_id);
    void log_ugc_settings_state(
            Unreal::UObject* subsystem,
            Unreal::UObject* package,
            const std::wstring& effective_mod_id);
}
