use std::fs;
use std::path::Path;

use crate::ini::ParsedIni;

const DEFAULT_SETTINGS: &str = r#"
[General]
EnableHotReloadSystem = 1
UseCache = 1
InvalidateCacheIfDLLDiffers = 1
SecondsToScanBeforeGivingUp = 30
bUseUObjectArrayCache = true
EnableSlowCppModUpdateGuard = true
SlowCppModUpdateThresholdMs = 4000

[EngineVersionOverride]
MajorVersion = -1
MinorVersion = -1

[Debug]
ConsoleEnabled = 0

[Threads]
SigScannerNumThreads = 8
SigScannerMultithreadingModuleSizeThreshold = 16777216

[Memory]
MaxMemoryUsageDuringAssetLoading = 80

[Hooks]
HookProcessInternal = 1
HookProcessLocalScriptFunction = 1
HookInitGameState = 1
HookCallFunctionByNameWithArguments = 1
HookBeginPlay  = 1
HookLocalPlayerExec = 1
HookEngineTick = 1
HookProcessEvent = 1
HookUFunctionBind = 1
FExecVTableOffsetInLocalPlayer = 0x28

[CrashDump]
EnableDumping = 1
FullMemoryDump = 0
"#;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SettingsSnapshot {
    pub general_enable_hot_reload_system: u8,
    pub general_use_cache: u8,
    pub general_invalidate_cache_if_dll_differs: u8,
    pub general_enable_debug_key_bindings: u8,
    pub general_seconds_to_scan_before_giving_up: i64,
    pub general_use_uobject_array_cache: u8,
    pub general_enable_slow_cpp_mod_update_guard: u8,
    pub general_slow_cpp_mod_update_threshold_ms: i64,
    pub engine_version_override_major_version: i64,
    pub engine_version_override_minor_version: i64,
    pub debug_simple_console_enabled: u8,
    pub debug_debug_console_enabled: u8,
    pub debug_debug_console_visible: u8,
    pub debug_debug_gui_font_scaling: f32,
    pub crash_dump_enable_dumping: u8,
    pub crash_dump_full_memory_dump: u8,
    pub threads_sig_scanner_num_threads: i64,
    pub threads_sig_scanner_multithreading_module_size_threshold: i64,
    pub memory_max_memory_usage_during_asset_loading: i64,
    pub hooks_hook_process_internal: u8,
    pub hooks_hook_process_local_script_function: u8,
    pub hooks_hook_init_game_state: u8,
    pub hooks_hook_load_map: u8,
    pub hooks_hook_call_function_by_name_with_arguments: u8,
    pub hooks_hook_begin_play: u8,
    pub hooks_hook_local_player_exec: u8,
    pub hooks_hook_engine_tick: u8,
    pub hooks_hook_aactor_tick: u8,
    pub hooks_hook_process_event: u8,
    pub hooks_hook_ufunction_bind: u8,
    pub hooks_fexec_vtable_offset_in_local_player: i64,
}

impl Default for SettingsSnapshot {
    fn default() -> Self {
        Self {
            general_enable_hot_reload_system: 0,
            general_use_cache: 1,
            general_invalidate_cache_if_dll_differs: 1,
            general_enable_debug_key_bindings: 0,
            general_seconds_to_scan_before_giving_up: 30,
            general_use_uobject_array_cache: 1,
            general_enable_slow_cpp_mod_update_guard: 1,
            general_slow_cpp_mod_update_threshold_ms: 4000,
            engine_version_override_major_version: -1,
            engine_version_override_minor_version: -1,
            debug_simple_console_enabled: 0,
            debug_debug_console_enabled: 0,
            debug_debug_console_visible: 0,
            debug_debug_gui_font_scaling: 1.0,
            crash_dump_enable_dumping: 1,
            crash_dump_full_memory_dump: 0,
            threads_sig_scanner_num_threads: 8,
            threads_sig_scanner_multithreading_module_size_threshold: 16_777_216,
            memory_max_memory_usage_during_asset_loading: 85,
            hooks_hook_process_internal: 1,
            hooks_hook_process_local_script_function: 0,
            hooks_hook_init_game_state: 1,
            hooks_hook_load_map: 1,
            hooks_hook_call_function_by_name_with_arguments: 1,
            hooks_hook_begin_play: 1,
            hooks_hook_local_player_exec: 1,
            hooks_hook_engine_tick: 1,
            hooks_hook_aactor_tick: 0,
            hooks_hook_process_event: 1,
            hooks_hook_ufunction_bind: 1,
            hooks_fexec_vtable_offset_in_local_player: 0x28,
        }
    }
}

pub fn load_settings(path: &Path) -> SettingsSnapshot {
    let contents = fs::read_to_string(path).unwrap_or_else(|_| DEFAULT_SETTINGS.to_owned());
    let ini = ParsedIni::parse(&contents);
    let mut snapshot = SettingsSnapshot::default();

    apply_bool(
        &ini,
        "General",
        "EnableHotReloadSystem",
        &mut snapshot.general_enable_hot_reload_system,
    );
    apply_bool(&ini, "General", "UseCache", &mut snapshot.general_use_cache);
    apply_bool(
        &ini,
        "General",
        "InvalidateCacheIfDLLDiffers",
        &mut snapshot.general_invalidate_cache_if_dll_differs,
    );
    apply_bool(
        &ini,
        "General",
        "EnableDebugKeyBindings",
        &mut snapshot.general_enable_debug_key_bindings,
    );
    apply_i64(
        &ini,
        "General",
        "SecondsToScanBeforeGivingUp",
        &mut snapshot.general_seconds_to_scan_before_giving_up,
    );
    apply_bool(
        &ini,
        "General",
        "bUseUObjectArrayCache",
        &mut snapshot.general_use_uobject_array_cache,
    );
    apply_bool(
        &ini,
        "General",
        "EnableSlowCppModUpdateGuard",
        &mut snapshot.general_enable_slow_cpp_mod_update_guard,
    );
    apply_i64(
        &ini,
        "General",
        "SlowCppModUpdateThresholdMs",
        &mut snapshot.general_slow_cpp_mod_update_threshold_ms,
    );

    apply_i64(
        &ini,
        "EngineVersionOverride",
        "MajorVersion",
        &mut snapshot.engine_version_override_major_version,
    );
    apply_i64(
        &ini,
        "EngineVersionOverride",
        "MinorVersion",
        &mut snapshot.engine_version_override_minor_version,
    );

    apply_bool(
        &ini,
        "Debug",
        "ConsoleEnabled",
        &mut snapshot.debug_simple_console_enabled,
    );
    apply_bool(
        &ini,
        "Debug",
        "GuiConsoleEnabled",
        &mut snapshot.debug_debug_console_enabled,
    );
    apply_bool(
        &ini,
        "Debug",
        "GuiConsoleVisible",
        &mut snapshot.debug_debug_console_visible,
    );
    apply_f32(
        &ini,
        "Debug",
        "GuiConsoleFontScaling",
        &mut snapshot.debug_debug_gui_font_scaling,
    );

    apply_bool(
        &ini,
        "CrashDump",
        "EnableDumping",
        &mut snapshot.crash_dump_enable_dumping,
    );
    apply_bool(
        &ini,
        "CrashDump",
        "FullMemoryDump",
        &mut snapshot.crash_dump_full_memory_dump,
    );

    apply_i64(
        &ini,
        "Threads",
        "SigScannerNumThreads",
        &mut snapshot.threads_sig_scanner_num_threads,
    );
    apply_i64(
        &ini,
        "Threads",
        "SigScannerMultithreadingModuleSizeThreshold",
        &mut snapshot.threads_sig_scanner_multithreading_module_size_threshold,
    );
    apply_i64(
        &ini,
        "Memory",
        "MaxMemoryUsageDuringAssetLoading",
        &mut snapshot.memory_max_memory_usage_during_asset_loading,
    );

    apply_bool(
        &ini,
        "Hooks",
        "HookProcessInternal",
        &mut snapshot.hooks_hook_process_internal,
    );
    apply_bool(
        &ini,
        "Hooks",
        "HookProcessLocalScriptFunction",
        &mut snapshot.hooks_hook_process_local_script_function,
    );
    apply_bool(
        &ini,
        "Hooks",
        "HookLoadMap",
        &mut snapshot.hooks_hook_load_map,
    );
    apply_bool(
        &ini,
        "Hooks",
        "HookInitGameState",
        &mut snapshot.hooks_hook_init_game_state,
    );
    apply_bool(
        &ini,
        "Hooks",
        "HookCallFunctionByNameWithArguments",
        &mut snapshot.hooks_hook_call_function_by_name_with_arguments,
    );
    apply_bool(
        &ini,
        "Hooks",
        "HookBeginPlay",
        &mut snapshot.hooks_hook_begin_play,
    );
    apply_bool(
        &ini,
        "Hooks",
        "HookLocalPlayerExec",
        &mut snapshot.hooks_hook_local_player_exec,
    );
    apply_bool(
        &ini,
        "Hooks",
        "HookEngineTick",
        &mut snapshot.hooks_hook_engine_tick,
    );
    apply_bool(
        &ini,
        "Hooks",
        "HookAActorTick",
        &mut snapshot.hooks_hook_aactor_tick,
    );
    apply_bool(
        &ini,
        "Hooks",
        "HookProcessEvent",
        &mut snapshot.hooks_hook_process_event,
    );
    apply_bool(
        &ini,
        "Hooks",
        "HookUFunctionBind",
        &mut snapshot.hooks_hook_ufunction_bind,
    );
    apply_i64(
        &ini,
        "Hooks",
        "FExecVTableOffsetInLocalPlayer",
        &mut snapshot.hooks_fexec_vtable_offset_in_local_player,
    );

    snapshot
}

fn apply_bool(ini: &ParsedIni, section: &str, key: &str, target: &mut u8) {
    let Some(value) = ini.get_bool(section, key) else {
        return;
    };
    *target = u8::from(value);
}

fn apply_i64(ini: &ParsedIni, section: &str, key: &str, target: &mut i64) {
    *target = ini.get_i64(section, key, *target);
}

fn apply_f32(ini: &ParsedIni, section: &str, key: &str, target: &mut f32) {
    let Some(value) = ini.get_f32(section, key) else {
        return;
    };
    *target = value;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_settings_match_legacy_fallback_values() {
        let ini = ParsedIni::parse(DEFAULT_SETTINGS);
        let mut snapshot = SettingsSnapshot::default();

        apply_bool(
            &ini,
            "Debug",
            "ConsoleEnabled",
            &mut snapshot.debug_simple_console_enabled,
        );
        apply_i64(
            &ini,
            "Memory",
            "MaxMemoryUsageDuringAssetLoading",
            &mut snapshot.memory_max_memory_usage_during_asset_loading,
        );

        assert_eq!(snapshot.debug_simple_console_enabled, 0);
        assert_eq!(snapshot.memory_max_memory_usage_during_asset_loading, 80);
    }
}
