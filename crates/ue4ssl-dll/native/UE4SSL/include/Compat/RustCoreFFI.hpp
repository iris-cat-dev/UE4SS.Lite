#pragma once

#include <Compat/GeneratedRustCoreAbi.hpp>

namespace RC::Compat::RustCore
{
    struct SettingsSnapshot
    {
        uint8_t general_enable_hot_reload_system{};
        uint8_t general_use_cache{};
        uint8_t general_invalidate_cache_if_dll_differs{};
        uint8_t general_enable_debug_key_bindings{};
        int64_t general_seconds_to_scan_before_giving_up{};
        uint8_t general_use_uobject_array_cache{};
        uint8_t general_enable_slow_cpp_mod_update_guard{};
        int64_t general_slow_cpp_mod_update_threshold_ms{};
        uint8_t object_search_use_native_static_find_object_fast{};
        uint8_t object_search_use_native_class_enumeration{};
        uint8_t object_search_compare_native_search_results{};
        int64_t engine_version_override_major_version{};
        int64_t engine_version_override_minor_version{};
        uint8_t debug_simple_console_enabled{};
        uint8_t debug_debug_console_enabled{};
        uint8_t debug_debug_console_visible{};
        float debug_debug_gui_font_scaling{};
        uint8_t crash_dump_enable_dumping{};
        uint8_t crash_dump_full_memory_dump{};
        int64_t threads_sig_scanner_num_threads{};
        int64_t threads_sig_scanner_multithreading_module_size_threshold{};
        int64_t memory_max_memory_usage_during_asset_loading{};
        uint8_t hooks_hook_process_internal{};
        uint8_t hooks_hook_process_local_script_function{};
        uint8_t hooks_hook_init_game_state{};
        uint8_t hooks_hook_load_map{};
        uint8_t hooks_hook_call_function_by_name_with_arguments{};
        uint8_t hooks_hook_begin_play{};
        uint8_t hooks_hook_local_player_exec{};
        uint8_t hooks_hook_engine_tick{};
        uint8_t hooks_hook_aactor_tick{};
        uint8_t hooks_hook_process_event{};
        uint8_t hooks_hook_ufunction_bind{};
        uint8_t hooks_hook_static_construct_object_object_cache{};
        int64_t hooks_fexec_vtable_offset_in_local_player{};
    };
}

extern "C"
{
    auto ue4ssl_core_compute_base_paths(RC::Compat::RustCore::SliceU16 module_file_path,
                                        RC::Compat::RustCore::SliceU16 game_exe_path)
            -> RC::Compat::RustCore::PathSnapshot;

    auto ue4ssl_core_resolve_mods_directory(RC::Compat::RustCore::SliceU16 working_directory,
                                            RC::Compat::RustCore::SliceU16 current_mods_directory,
                                            RC::Compat::RustCore::SliceU16 override_mods_directory)
            -> RC::Compat::RustCore::OwnedString;

    auto ue4ssl_core_discover_mods(RC::Compat::RustCore::SliceU16 working_directory, RC::Compat::RustCore::SliceU16 mods_directory)
            -> RC::Compat::RustCore::ModDiscovery;
    auto ue4ssl_core_load_settings(RC::Compat::RustCore::SliceU16 settings_path) -> RC::Compat::RustCore::SettingsSnapshot;
    struct Ue4sslCoreIniHandle;
    auto ue4ssl_core_ini_parse(RC::Compat::RustCore::SliceU16 contents) -> Ue4sslCoreIniHandle*;
    auto ue4ssl_core_ini_destroy(Ue4sslCoreIniHandle* handle) -> void;
    auto ue4ssl_core_ini_get_i64(Ue4sslCoreIniHandle* handle,
                                 RC::Compat::RustCore::SliceU16 section,
                                 RC::Compat::RustCore::SliceU16 key,
                                 int64_t default_value) -> int64_t;
    auto ue4ssl_core_ini_ordered_list_len(Ue4sslCoreIniHandle* handle, RC::Compat::RustCore::SliceU16 section) -> size_t;
    auto ue4ssl_core_ini_ordered_list_item(Ue4sslCoreIniHandle* handle,
                                           RC::Compat::RustCore::SliceU16 section,
                                           size_t index) -> RC::Compat::RustCore::OwnedString;

    auto ue4ssl_core_get_program_flags() -> RC::Compat::RustCore::ProgramFlags;
    auto ue4ssl_core_set_program_started(uint8_t is_program_started) -> void;
    auto ue4ssl_core_set_processing_state(uint8_t processing_events, uint8_t pause_events_processing) -> void;

    auto ue4ssl_core_cppmod_create(RC::Compat::RustCore::SliceU16 mod_path, RC::Compat::RustCore::SliceU16 dll_name)
            -> RC::Compat::RustCore::CppModHandle*;
    auto ue4ssl_core_cppmod_destroy(RC::Compat::RustCore::CppModHandle* handle) -> void;
    auto ue4ssl_core_cppmod_status(const RC::Compat::RustCore::CppModHandle* handle)
            -> RC::Compat::RustCore::CppModRuntimeStatus;
    auto ue4ssl_core_cppmod_set_installable(RC::Compat::RustCore::CppModHandle* handle, uint8_t value) -> void;
    auto ue4ssl_core_cppmod_set_installed(RC::Compat::RustCore::CppModHandle* handle, uint8_t value) -> void;
    auto ue4ssl_core_cppmod_set_updates_disabled(RC::Compat::RustCore::CppModHandle* handle, uint8_t value) -> void;
    auto ue4ssl_core_cppmod_start(RC::Compat::RustCore::CppModHandle* handle) -> void;
    auto ue4ssl_core_cppmod_uninstall(RC::Compat::RustCore::CppModHandle* handle) -> void;
    auto ue4ssl_core_cppmod_fire_unreal_init(RC::Compat::RustCore::CppModHandle* handle) -> void;
    auto ue4ssl_core_cppmod_fire_ui_init(RC::Compat::RustCore::CppModHandle* handle) -> void;
    auto ue4ssl_core_cppmod_fire_program_start(RC::Compat::RustCore::CppModHandle* handle) -> void;
    auto ue4ssl_core_cppmod_fire_update(RC::Compat::RustCore::CppModHandle* handle) -> void;
    auto ue4ssl_core_cppmod_fire_dll_load(RC::Compat::RustCore::CppModHandle* handle,
                                          RC::Compat::RustCore::SliceU16 dll_name) -> void;

    auto ue4ssl_core_free_string(RC::Compat::RustCore::OwnedString string) -> void;
    auto ue4ssl_core_free_path_snapshot(RC::Compat::RustCore::PathSnapshot snapshot) -> void;
    auto ue4ssl_core_free_mod_discovery(RC::Compat::RustCore::ModDiscovery discovery) -> void;
}
