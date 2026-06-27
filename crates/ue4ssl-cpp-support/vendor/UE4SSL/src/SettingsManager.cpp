#include <Compat/RustCoreFFI.hpp>
#include <SettingsManager.hpp>

namespace
{
    static_assert(sizeof(RC::CharType) == sizeof(uint16_t));

    auto make_slice(const std::filesystem::path& value) -> RC::Compat::RustCore::SliceU16
    {
        const auto& native = value.native();
        return {
                reinterpret_cast<const uint16_t*>(native.data()),
                native.size(),
        };
    }

    auto to_bool(uint8_t value) -> bool
    {
        return value != 0;
    }
} // namespace

namespace RC
{


    auto SettingsManager::deserialize(std::filesystem::path& file_name) -> void
    {
        const auto settings = ue4ssl_core_load_settings(make_slice(file_name));

        General.EnableHotReloadSystem = to_bool(settings.general_enable_hot_reload_system);
        General.UseCache = to_bool(settings.general_use_cache);
        General.InvalidateCacheIfDLLDiffers = to_bool(settings.general_invalidate_cache_if_dll_differs);
        General.EnableDebugKeyBindings = to_bool(settings.general_enable_debug_key_bindings);
        General.SecondsToScanBeforeGivingUp = settings.general_seconds_to_scan_before_giving_up;
        General.UseUObjectArrayCache = to_bool(settings.general_use_uobject_array_cache);
        General.EnableSlowCppModUpdateGuard = to_bool(settings.general_enable_slow_cpp_mod_update_guard);
        General.SlowCppModUpdateThresholdMs = settings.general_slow_cpp_mod_update_threshold_ms;

        ObjectSearch.UseNativeStaticFindObjectFast = to_bool(settings.object_search_use_native_static_find_object_fast);
        ObjectSearch.UseNativeClassEnumeration = to_bool(settings.object_search_use_native_class_enumeration);
        ObjectSearch.CompareNativeSearchResults = to_bool(settings.object_search_compare_native_search_results);

        EngineVersionOverride.MajorVersion = settings.engine_version_override_major_version;
        EngineVersionOverride.MinorVersion = settings.engine_version_override_minor_version;

        Debug.SimpleConsoleEnabled = to_bool(settings.debug_simple_console_enabled);
        Debug.DebugConsoleEnabled = to_bool(settings.debug_debug_console_enabled);
        Debug.DebugConsoleVisible = to_bool(settings.debug_debug_console_visible);
        Debug.DebugGUIFontScaling = settings.debug_debug_gui_font_scaling;

        CrashDump.EnableDumping = to_bool(settings.crash_dump_enable_dumping);
        CrashDump.FullMemoryDump = to_bool(settings.crash_dump_full_memory_dump);

        Threads.SigScannerNumThreads = settings.threads_sig_scanner_num_threads;
        Threads.SigScannerMultithreadingModuleSizeThreshold = settings.threads_sig_scanner_multithreading_module_size_threshold;

        Memory.MaxMemoryUsageDuringAssetLoading = settings.memory_max_memory_usage_during_asset_loading;

        Hooks.HookProcessInternal = to_bool(settings.hooks_hook_process_internal);
        Hooks.HookProcessLocalScriptFunction = to_bool(settings.hooks_hook_process_local_script_function);
        Hooks.HookLoadMap = to_bool(settings.hooks_hook_load_map);
        Hooks.HookInitGameState = to_bool(settings.hooks_hook_init_game_state);
        Hooks.HookCallFunctionByNameWithArguments = to_bool(settings.hooks_hook_call_function_by_name_with_arguments);
        Hooks.HookBeginPlay = to_bool(settings.hooks_hook_begin_play);
        Hooks.HookLocalPlayerExec = to_bool(settings.hooks_hook_local_player_exec);
        Hooks.HookEngineTick = to_bool(settings.hooks_hook_engine_tick);
        Hooks.HookAActorTick = to_bool(settings.hooks_hook_aactor_tick);
        Hooks.HookProcessEvent = to_bool(settings.hooks_hook_process_event);
        Hooks.HookUFunctionBind = to_bool(settings.hooks_hook_ufunction_bind);
        Hooks.HookStaticConstructObjectObjectCache = to_bool(settings.hooks_hook_static_construct_object_object_cache);
        Hooks.FExecVTableOffsetInLocalPlayer = settings.hooks_fexec_vtable_offset_in_local_player;
    }
} // namespace RC
