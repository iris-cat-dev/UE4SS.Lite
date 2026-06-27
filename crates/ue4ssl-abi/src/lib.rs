use std::fmt::Write as _;
use std::mem::{align_of, offset_of, size_of};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct SliceU16 {
    pub data: *const u16,
    pub len: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct OwnedString {
    pub data: *mut u16,
    pub len: usize,
}

#[repr(C)]
#[derive(Default)]
pub struct PathSnapshot {
    pub root_directory: OwnedString,
    pub working_directory: OwnedString,
    pub mods_directory: OwnedString,
    pub game_executable_directory: OwnedString,
    pub settings_path_and_file: OwnedString,
    pub legacy_root_directory: OwnedString,
    pub object_dumper_output_directory: OwnedString,
    pub log_directory: OwnedString,
    pub game_path_and_exe_name: OwnedString,
    pub has_game_specific_config: u8,
}

#[repr(C)]
#[derive(Default)]
pub struct DiscoveredMod {
    pub mod_name: OwnedString,
    pub mod_path: OwnedString,
    pub dll_name: OwnedString,
    pub has_custom_dll_name: u8,
    pub is_builtin: u8,
}

#[repr(C)]
#[derive(Default)]
pub struct ModDiscovery {
    pub mods: *mut DiscoveredMod,
    pub len: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ProgramFlags {
    pub is_program_started: u8,
    pub processing_events: u8,
    pub pause_events_processing: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct CppModRuntimeStatus {
    pub installable: u8,
    pub installed: u8,
    pub started: u8,
    pub updates_disabled: u8,
    pub failure_code: u32,
    pub last_error: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct RustModStartContext {
    pub mod_path: SliceU16,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HostVector {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HostHookHandle {
    pub function: *mut std::ffi::c_void,
    pub pre_id: i32,
    pub post_id: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HostHookContext {
    pub context: *mut std::ffi::c_void,
}

pub type PsLogFn = extern "C" fn(*const u16);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PsCtx {
    pub default_fn: PsLogFn,
    pub normal_fn: PsLogFn,
    pub verbose_fn: PsLogFn,
    pub warning_fn: PsLogFn,
    pub error_fn: PsLogFn,
    pub config: PsScanConfig,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PsEngineVersion {
    pub major: u16,
    pub minor: u16,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PsScanConfig {
    pub guobject_array: u8,
    pub fname_tostring: u8,
    pub fname_ctor_wchar: u8,
    pub gmalloc: u8,
    pub static_construct_object_internal: u8,
    pub ftext_fstring: u8,
    pub engine_version: u8,
    pub ufunction_bind: u8,
    pub fuobject_hash_tables_get: u8,
    pub gnatives: u8,
    pub console_manager_singleton: u8,
    pub gameengine_tick: u8,
    pub static_find_object_fast: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PsScanResults {
    pub guobject_array: usize,
    pub fname_tostring: usize,
    pub fname_ctor_wchar: usize,
    pub gmalloc: usize,
    pub static_construct_object_internal: usize,
    pub ftext_fstring: usize,
    pub engine_version: PsEngineVersion,
    pub ufunction_bind: usize,
    pub fuobject_hash_tables_get: usize,
    pub gnatives: usize,
    pub console_manager_singleton: usize,
    pub gameengine_tick: usize,
    pub static_find_object_fast: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HostReinstallPlan {
    pub should_pause_processing: u8,
    pub should_resume_before_restart: u8,
    pub should_fire_unreal_init: u8,
    pub should_fire_program_start: u8,
}

pub const HOST_REINSTALL_SEQUENCE_CAPACITY: usize = 16;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HostReinstallStep {
    pub action: u32,
    pub value: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct HostReinstallSequence {
    pub steps: [HostReinstallStep; HOST_REINSTALL_SEQUENCE_CAPACITY],
    pub len: usize,
}

impl Default for HostReinstallSequence {
    fn default() -> Self {
        Self {
            steps: [HostReinstallStep::default(); HOST_REINSTALL_SEQUENCE_CAPACITY],
            len: 0,
        }
    }
}

pub const HOST_MOD_STARTUP_SEQUENCE_CAPACITY: usize = 8;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HostModStartupStep {
    pub action: u32,
    pub mod_name: *const u16,
    pub mod_name_len: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct HostModStartupSequence {
    pub steps: [HostModStartupStep; HOST_MOD_STARTUP_SEQUENCE_CAPACITY],
    pub len: usize,
}

impl Default for HostModStartupSequence {
    fn default() -> Self {
        Self {
            steps: [HostModStartupStep::default(); HOST_MOD_STARTUP_SEQUENCE_CAPACITY],
            len: 0,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HostUnrealConfigPlan {
    pub num_scan_threads: u32,
    pub multithreading_module_size_threshold: u32,
    pub engine_version_major: u32,
    pub engine_version_minor: u32,
    pub has_num_scan_threads: u8,
    pub has_multithreading_module_size_threshold: u8,
    pub has_engine_version_override: u8,
    pub engine_version_override_invalid: u8,
    pub should_enable_builtin_guobjectarray_fallback: u8,
    pub fexec_vtable_offset_in_local_player: u8,
    pub is_fexec_vtable_offset_in_local_player_in_range: u8,
}

fn render_file_prelude(guard: &str) -> String {
    let mut output = String::new();
    writeln!(output, "#pragma once").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "#ifndef {guard}").unwrap();
    writeln!(output, "#define {guard}").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "#include <cstddef>").unwrap();
    writeln!(output, "#include <cstdint>").unwrap();
    writeln!(output).unwrap();
    output
}

fn render_layout_asserts(
    output: &mut String,
    struct_name: &str,
    size: usize,
    align: usize,
    fields: &[(&str, usize)],
) {
    writeln!(
        output,
        "    static_assert(sizeof({struct_name}) == {size}, \"{struct_name} size mismatch\");"
    )
    .unwrap();
    writeln!(
        output,
        "    static_assert(alignof({struct_name}) == {align}, \"{struct_name} align mismatch\");"
    )
    .unwrap();
    for (field_name, offset) in fields {
        writeln!(
            output,
            "    static_assert(offsetof({struct_name}, {field_name}) == {offset}, \"{struct_name}.{field_name} offset mismatch\");"
        )
        .unwrap();
    }
    writeln!(output).unwrap();
}

pub fn render_rustcore_header() -> String {
    let mut output = render_file_prelude("UE4SSL_GENERATED_RUSTCORE_ABI_HPP");
    writeln!(output, "namespace RC::Compat::RustCore").unwrap();
    writeln!(output, "{{").unwrap();
    writeln!(output, "    struct SliceU16").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        const uint16_t* data{{}};").unwrap();
    writeln!(output, "        size_t len{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct OwnedString").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        uint16_t* data{{}};").unwrap();
    writeln!(output, "        size_t len{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct PathSnapshot").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        OwnedString root_directory{{}};").unwrap();
    writeln!(output, "        OwnedString working_directory{{}};").unwrap();
    writeln!(output, "        OwnedString mods_directory{{}};").unwrap();
    writeln!(output, "        OwnedString game_executable_directory{{}};").unwrap();
    writeln!(output, "        OwnedString settings_path_and_file{{}};").unwrap();
    writeln!(output, "        OwnedString legacy_root_directory{{}};").unwrap();
    writeln!(
        output,
        "        OwnedString object_dumper_output_directory{{}};"
    )
    .unwrap();
    writeln!(output, "        OwnedString log_directory{{}};").unwrap();
    writeln!(output, "        OwnedString game_path_and_exe_name{{}};").unwrap();
    writeln!(output, "        uint8_t has_game_specific_config{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct DiscoveredMod").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        OwnedString mod_name{{}};").unwrap();
    writeln!(output, "        OwnedString mod_path{{}};").unwrap();
    writeln!(output, "        OwnedString dll_name{{}};").unwrap();
    writeln!(output, "        uint8_t has_custom_dll_name{{}};").unwrap();
    writeln!(output, "        uint8_t is_builtin{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct ModDiscovery").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        DiscoveredMod* mods{{}};").unwrap();
    writeln!(output, "        size_t len{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct ProgramFlags").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        uint8_t is_program_started{{}};").unwrap();
    writeln!(output, "        uint8_t processing_events{{}};").unwrap();
    writeln!(output, "        uint8_t pause_events_processing{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct CppModHandle;").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct CppModRuntimeStatus").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        uint8_t installable{{}};").unwrap();
    writeln!(output, "        uint8_t installed{{}};").unwrap();
    writeln!(output, "        uint8_t started{{}};").unwrap();
    writeln!(output, "        uint8_t updates_disabled{{}};").unwrap();
    writeln!(output, "        uint32_t failure_code{{}};").unwrap();
    writeln!(output, "        uint32_t last_error{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct RustModStartContext").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        SliceU16 mod_path{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct HostVector").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        double x{{}};").unwrap();
    writeln!(output, "        double y{{}};").unwrap();
    writeln!(output, "        double z{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct HostHookHandle").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        void* function{{}};").unwrap();
    writeln!(output, "        int32_t pre_id{{}};").unwrap();
    writeln!(output, "        int32_t post_id{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct HostHookContext").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        void* context{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    render_layout_asserts(
        &mut output,
        "SliceU16",
        size_of::<SliceU16>(),
        align_of::<SliceU16>(),
        &[
            ("data", offset_of!(SliceU16, data)),
            ("len", offset_of!(SliceU16, len)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "OwnedString",
        size_of::<OwnedString>(),
        align_of::<OwnedString>(),
        &[
            ("data", offset_of!(OwnedString, data)),
            ("len", offset_of!(OwnedString, len)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "PathSnapshot",
        size_of::<PathSnapshot>(),
        align_of::<PathSnapshot>(),
        &[
            ("root_directory", offset_of!(PathSnapshot, root_directory)),
            (
                "working_directory",
                offset_of!(PathSnapshot, working_directory),
            ),
            ("mods_directory", offset_of!(PathSnapshot, mods_directory)),
            (
                "game_executable_directory",
                offset_of!(PathSnapshot, game_executable_directory),
            ),
            (
                "settings_path_and_file",
                offset_of!(PathSnapshot, settings_path_and_file),
            ),
            (
                "legacy_root_directory",
                offset_of!(PathSnapshot, legacy_root_directory),
            ),
            (
                "object_dumper_output_directory",
                offset_of!(PathSnapshot, object_dumper_output_directory),
            ),
            ("log_directory", offset_of!(PathSnapshot, log_directory)),
            (
                "game_path_and_exe_name",
                offset_of!(PathSnapshot, game_path_and_exe_name),
            ),
            (
                "has_game_specific_config",
                offset_of!(PathSnapshot, has_game_specific_config),
            ),
        ],
    );
    render_layout_asserts(
        &mut output,
        "DiscoveredMod",
        size_of::<DiscoveredMod>(),
        align_of::<DiscoveredMod>(),
        &[
            ("mod_name", offset_of!(DiscoveredMod, mod_name)),
            ("mod_path", offset_of!(DiscoveredMod, mod_path)),
            ("dll_name", offset_of!(DiscoveredMod, dll_name)),
            (
                "has_custom_dll_name",
                offset_of!(DiscoveredMod, has_custom_dll_name),
            ),
            ("is_builtin", offset_of!(DiscoveredMod, is_builtin)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "ModDiscovery",
        size_of::<ModDiscovery>(),
        align_of::<ModDiscovery>(),
        &[
            ("mods", offset_of!(ModDiscovery, mods)),
            ("len", offset_of!(ModDiscovery, len)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "ProgramFlags",
        size_of::<ProgramFlags>(),
        align_of::<ProgramFlags>(),
        &[
            (
                "is_program_started",
                offset_of!(ProgramFlags, is_program_started),
            ),
            (
                "processing_events",
                offset_of!(ProgramFlags, processing_events),
            ),
            (
                "pause_events_processing",
                offset_of!(ProgramFlags, pause_events_processing),
            ),
        ],
    );
    render_layout_asserts(
        &mut output,
        "CppModRuntimeStatus",
        size_of::<CppModRuntimeStatus>(),
        align_of::<CppModRuntimeStatus>(),
        &[
            ("installable", offset_of!(CppModRuntimeStatus, installable)),
            ("installed", offset_of!(CppModRuntimeStatus, installed)),
            ("started", offset_of!(CppModRuntimeStatus, started)),
            (
                "updates_disabled",
                offset_of!(CppModRuntimeStatus, updates_disabled),
            ),
            (
                "failure_code",
                offset_of!(CppModRuntimeStatus, failure_code),
            ),
            ("last_error", offset_of!(CppModRuntimeStatus, last_error)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "RustModStartContext",
        size_of::<RustModStartContext>(),
        align_of::<RustModStartContext>(),
        &[("mod_path", offset_of!(RustModStartContext, mod_path))],
    );
    render_layout_asserts(
        &mut output,
        "HostVector",
        size_of::<HostVector>(),
        align_of::<HostVector>(),
        &[
            ("x", offset_of!(HostVector, x)),
            ("y", offset_of!(HostVector, y)),
            ("z", offset_of!(HostVector, z)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "HostHookHandle",
        size_of::<HostHookHandle>(),
        align_of::<HostHookHandle>(),
        &[
            ("function", offset_of!(HostHookHandle, function)),
            ("pre_id", offset_of!(HostHookHandle, pre_id)),
            ("post_id", offset_of!(HostHookHandle, post_id)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "HostHookContext",
        size_of::<HostHookContext>(),
        align_of::<HostHookContext>(),
        &[("context", offset_of!(HostHookContext, context))],
    );
    writeln!(output, "}}").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "#endif // UE4SSL_GENERATED_RUSTCORE_ABI_HPP").unwrap();
    output
}

pub fn render_scan_header() -> String {
    let mut output = render_file_prelude("UE4SSL_GENERATED_SCAN_ABI_HPP");
    writeln!(output, "namespace RC::Compat::Scan").unwrap();
    writeln!(output, "{{").unwrap();
    writeln!(output, "    using LogFn = void (*)(const uint16_t* msg);").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct PsEngineVersion").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        uint16_t major{{}};").unwrap();
    writeln!(output, "        uint16_t minor{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct PsScanConfig").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        uint8_t guobject_array{{}};").unwrap();
    writeln!(output, "        uint8_t fname_tostring{{}};").unwrap();
    writeln!(output, "        uint8_t fname_ctor_wchar{{}};").unwrap();
    writeln!(output, "        uint8_t gmalloc{{}};").unwrap();
    writeln!(
        output,
        "        uint8_t static_construct_object_internal{{}};"
    )
    .unwrap();
    writeln!(output, "        uint8_t ftext_fstring{{}};").unwrap();
    writeln!(output, "        uint8_t engine_version{{}};").unwrap();
    writeln!(output, "        uint8_t ufunction_bind{{}};").unwrap();
    writeln!(output, "        uint8_t fuobject_hash_tables_get{{}};").unwrap();
    writeln!(output, "        uint8_t gnatives{{}};").unwrap();
    writeln!(output, "        uint8_t console_manager_singleton{{}};").unwrap();
    writeln!(output, "        uint8_t gameengine_tick{{}};").unwrap();
    writeln!(output, "        uint8_t static_find_object_fast{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct PsCtx").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        LogFn default_fn{{}};").unwrap();
    writeln!(output, "        LogFn normal_fn{{}};").unwrap();
    writeln!(output, "        LogFn verbose_fn{{}};").unwrap();
    writeln!(output, "        LogFn warning_fn{{}};").unwrap();
    writeln!(output, "        LogFn error_fn{{}};").unwrap();
    writeln!(output, "        PsScanConfig config{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct PsScanResults").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        size_t guobject_array{{}};").unwrap();
    writeln!(output, "        size_t fname_tostring{{}};").unwrap();
    writeln!(output, "        size_t fname_ctor_wchar{{}};").unwrap();
    writeln!(output, "        size_t gmalloc{{}};").unwrap();
    writeln!(
        output,
        "        size_t static_construct_object_internal{{}};"
    )
    .unwrap();
    writeln!(output, "        size_t ftext_fstring{{}};").unwrap();
    writeln!(output, "        PsEngineVersion engine_version{{}};").unwrap();
    writeln!(output, "        size_t ufunction_bind{{}};").unwrap();
    writeln!(output, "        size_t fuobject_hash_tables_get{{}};").unwrap();
    writeln!(output, "        size_t gnatives{{}};").unwrap();
    writeln!(output, "        size_t console_manager_singleton{{}};").unwrap();
    writeln!(output, "        size_t gameengine_tick{{}};").unwrap();
    writeln!(output, "        size_t static_find_object_fast{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    render_layout_asserts(
        &mut output,
        "PsEngineVersion",
        size_of::<PsEngineVersion>(),
        align_of::<PsEngineVersion>(),
        &[
            ("major", offset_of!(PsEngineVersion, major)),
            ("minor", offset_of!(PsEngineVersion, minor)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "PsScanConfig",
        size_of::<PsScanConfig>(),
        align_of::<PsScanConfig>(),
        &[
            ("guobject_array", offset_of!(PsScanConfig, guobject_array)),
            ("fname_tostring", offset_of!(PsScanConfig, fname_tostring)),
            (
                "fname_ctor_wchar",
                offset_of!(PsScanConfig, fname_ctor_wchar),
            ),
            ("gmalloc", offset_of!(PsScanConfig, gmalloc)),
            (
                "static_construct_object_internal",
                offset_of!(PsScanConfig, static_construct_object_internal),
            ),
            ("ftext_fstring", offset_of!(PsScanConfig, ftext_fstring)),
            ("engine_version", offset_of!(PsScanConfig, engine_version)),
            ("ufunction_bind", offset_of!(PsScanConfig, ufunction_bind)),
            (
                "fuobject_hash_tables_get",
                offset_of!(PsScanConfig, fuobject_hash_tables_get),
            ),
            ("gnatives", offset_of!(PsScanConfig, gnatives)),
            (
                "console_manager_singleton",
                offset_of!(PsScanConfig, console_manager_singleton),
            ),
            ("gameengine_tick", offset_of!(PsScanConfig, gameengine_tick)),
            (
                "static_find_object_fast",
                offset_of!(PsScanConfig, static_find_object_fast),
            ),
        ],
    );
    render_layout_asserts(
        &mut output,
        "PsCtx",
        size_of::<PsCtx>(),
        align_of::<PsCtx>(),
        &[
            ("default_fn", offset_of!(PsCtx, default_fn)),
            ("normal_fn", offset_of!(PsCtx, normal_fn)),
            ("verbose_fn", offset_of!(PsCtx, verbose_fn)),
            ("warning_fn", offset_of!(PsCtx, warning_fn)),
            ("error_fn", offset_of!(PsCtx, error_fn)),
            ("config", offset_of!(PsCtx, config)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "PsScanResults",
        size_of::<PsScanResults>(),
        align_of::<PsScanResults>(),
        &[
            ("guobject_array", offset_of!(PsScanResults, guobject_array)),
            ("fname_tostring", offset_of!(PsScanResults, fname_tostring)),
            (
                "fname_ctor_wchar",
                offset_of!(PsScanResults, fname_ctor_wchar),
            ),
            ("gmalloc", offset_of!(PsScanResults, gmalloc)),
            (
                "static_construct_object_internal",
                offset_of!(PsScanResults, static_construct_object_internal),
            ),
            ("ftext_fstring", offset_of!(PsScanResults, ftext_fstring)),
            ("engine_version", offset_of!(PsScanResults, engine_version)),
            ("ufunction_bind", offset_of!(PsScanResults, ufunction_bind)),
            (
                "fuobject_hash_tables_get",
                offset_of!(PsScanResults, fuobject_hash_tables_get),
            ),
            ("gnatives", offset_of!(PsScanResults, gnatives)),
            (
                "console_manager_singleton",
                offset_of!(PsScanResults, console_manager_singleton),
            ),
            (
                "gameengine_tick",
                offset_of!(PsScanResults, gameengine_tick),
            ),
            (
                "static_find_object_fast",
                offset_of!(PsScanResults, static_find_object_fast),
            ),
        ],
    );
    writeln!(output, "}}").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "#endif // UE4SSL_GENERATED_SCAN_ABI_HPP").unwrap();
    output
}

pub fn render_host_header() -> String {
    let mut output = render_file_prelude("UE4SSL_GENERATED_HOST_ABI_HPP");
    writeln!(output, "namespace RC::Compat::Host").unwrap();
    writeln!(output, "{{").unwrap();
    writeln!(output, "    struct ReinstallPlan").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        uint8_t should_pause_processing{{}};").unwrap();
    writeln!(output, "        uint8_t should_resume_before_restart{{}};").unwrap();
    writeln!(output, "        uint8_t should_fire_unreal_init{{}};").unwrap();
    writeln!(output, "        uint8_t should_fire_program_start{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    enum class ReinstallAction : uint32_t").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        ResetDllDispatchCache = 1,").unwrap();
    writeln!(output, "        SetPauseProcessing = 2,").unwrap();
    writeln!(output, "        UninstallMods = 3,").unwrap();
    writeln!(output, "        RemoveScriptKeybinds = 4,").unwrap();
    writeln!(output, "        SetupMods = 5,").unwrap();
    writeln!(output, "        StartCppMods = 6,").unwrap();
    writeln!(output, "        FireUnrealInit = 7,").unwrap();
    writeln!(output, "        FireProgramStart = 8,").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct ReinstallStep").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        uint32_t action{{}};").unwrap();
    writeln!(output, "        uint8_t value{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(
        output,
        "    constexpr size_t ReinstallSequenceCapacity = {HOST_REINSTALL_SEQUENCE_CAPACITY};"
    )
    .unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct ReinstallSequence").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(
        output,
        "        ReinstallStep steps[ReinstallSequenceCapacity]{{}};"
    )
    .unwrap();
    writeln!(output, "        size_t len{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    enum class ModStartupAction : uint32_t").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        StartNamedMod = 1,").unwrap();
    writeln!(output, "        StartDiscoveredMods = 2,").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct ModStartupStep").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        uint32_t action{{}};").unwrap();
    writeln!(output, "        const uint16_t* mod_name{{}};").unwrap();
    writeln!(output, "        size_t mod_name_len{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(
        output,
        "    constexpr size_t ModStartupSequenceCapacity = {HOST_MOD_STARTUP_SEQUENCE_CAPACITY};"
    )
    .unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct ModStartupSequence").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(
        output,
        "        ModStartupStep steps[ModStartupSequenceCapacity]{{}};"
    )
    .unwrap();
    writeln!(output, "        size_t len{{}};").unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "    struct UnrealConfigPlan").unwrap();
    writeln!(output, "    {{").unwrap();
    writeln!(output, "        uint32_t num_scan_threads{{}};").unwrap();
    writeln!(
        output,
        "        uint32_t multithreading_module_size_threshold{{}};"
    )
    .unwrap();
    writeln!(output, "        uint32_t engine_version_major{{}};").unwrap();
    writeln!(output, "        uint32_t engine_version_minor{{}};").unwrap();
    writeln!(output, "        uint8_t has_num_scan_threads{{}};").unwrap();
    writeln!(
        output,
        "        uint8_t has_multithreading_module_size_threshold{{}};"
    )
    .unwrap();
    writeln!(output, "        uint8_t has_engine_version_override{{}};").unwrap();
    writeln!(
        output,
        "        uint8_t engine_version_override_invalid{{}};"
    )
    .unwrap();
    writeln!(
        output,
        "        uint8_t should_enable_builtin_guobjectarray_fallback{{}};"
    )
    .unwrap();
    writeln!(
        output,
        "        uint8_t fexec_vtable_offset_in_local_player{{}};"
    )
    .unwrap();
    writeln!(
        output,
        "        uint8_t is_fexec_vtable_offset_in_local_player_in_range{{}};"
    )
    .unwrap();
    writeln!(output, "    }};").unwrap();
    writeln!(output).unwrap();
    render_layout_asserts(
        &mut output,
        "ReinstallPlan",
        size_of::<HostReinstallPlan>(),
        align_of::<HostReinstallPlan>(),
        &[
            (
                "should_pause_processing",
                offset_of!(HostReinstallPlan, should_pause_processing),
            ),
            (
                "should_resume_before_restart",
                offset_of!(HostReinstallPlan, should_resume_before_restart),
            ),
            (
                "should_fire_unreal_init",
                offset_of!(HostReinstallPlan, should_fire_unreal_init),
            ),
            (
                "should_fire_program_start",
                offset_of!(HostReinstallPlan, should_fire_program_start),
            ),
        ],
    );
    render_layout_asserts(
        &mut output,
        "ReinstallStep",
        size_of::<HostReinstallStep>(),
        align_of::<HostReinstallStep>(),
        &[
            ("action", offset_of!(HostReinstallStep, action)),
            ("value", offset_of!(HostReinstallStep, value)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "ReinstallSequence",
        size_of::<HostReinstallSequence>(),
        align_of::<HostReinstallSequence>(),
        &[
            ("steps", offset_of!(HostReinstallSequence, steps)),
            ("len", offset_of!(HostReinstallSequence, len)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "ModStartupStep",
        size_of::<HostModStartupStep>(),
        align_of::<HostModStartupStep>(),
        &[
            ("action", offset_of!(HostModStartupStep, action)),
            ("mod_name", offset_of!(HostModStartupStep, mod_name)),
            ("mod_name_len", offset_of!(HostModStartupStep, mod_name_len)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "ModStartupSequence",
        size_of::<HostModStartupSequence>(),
        align_of::<HostModStartupSequence>(),
        &[
            ("steps", offset_of!(HostModStartupSequence, steps)),
            ("len", offset_of!(HostModStartupSequence, len)),
        ],
    );
    render_layout_asserts(
        &mut output,
        "UnrealConfigPlan",
        size_of::<HostUnrealConfigPlan>(),
        align_of::<HostUnrealConfigPlan>(),
        &[
            (
                "num_scan_threads",
                offset_of!(HostUnrealConfigPlan, num_scan_threads),
            ),
            (
                "multithreading_module_size_threshold",
                offset_of!(HostUnrealConfigPlan, multithreading_module_size_threshold),
            ),
            (
                "engine_version_major",
                offset_of!(HostUnrealConfigPlan, engine_version_major),
            ),
            (
                "engine_version_minor",
                offset_of!(HostUnrealConfigPlan, engine_version_minor),
            ),
            (
                "has_num_scan_threads",
                offset_of!(HostUnrealConfigPlan, has_num_scan_threads),
            ),
            (
                "has_multithreading_module_size_threshold",
                offset_of!(
                    HostUnrealConfigPlan,
                    has_multithreading_module_size_threshold
                ),
            ),
            (
                "has_engine_version_override",
                offset_of!(HostUnrealConfigPlan, has_engine_version_override),
            ),
            (
                "engine_version_override_invalid",
                offset_of!(HostUnrealConfigPlan, engine_version_override_invalid),
            ),
            (
                "should_enable_builtin_guobjectarray_fallback",
                offset_of!(
                    HostUnrealConfigPlan,
                    should_enable_builtin_guobjectarray_fallback
                ),
            ),
            (
                "fexec_vtable_offset_in_local_player",
                offset_of!(HostUnrealConfigPlan, fexec_vtable_offset_in_local_player),
            ),
            (
                "is_fexec_vtable_offset_in_local_player_in_range",
                offset_of!(
                    HostUnrealConfigPlan,
                    is_fexec_vtable_offset_in_local_player_in_range
                ),
            ),
        ],
    );
    writeln!(output, "}}").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "#endif // UE4SSL_GENERATED_HOST_ABI_HPP").unwrap();
    output
}

#[cfg(test)]
mod tests {
    use super::{render_host_header, render_rustcore_header, render_scan_header};

    #[test]
    fn rustcore_header_contains_layout_asserts() {
        let header = render_rustcore_header();
        assert!(header.contains("static_assert(sizeof(PathSnapshot)"));
        assert!(header.contains("offsetof(PathSnapshot, mods_directory)"));
    }

    #[test]
    fn scan_header_contains_layout_asserts() {
        let header = render_scan_header();
        assert!(header.contains("static_assert(sizeof(PsCtx)"));
        assert!(header.contains("offsetof(PsScanResults, gameengine_tick)"));
    }

    #[test]
    fn host_header_contains_layout_asserts() {
        let header = render_host_header();
        assert!(header.contains("static_assert(sizeof(ReinstallPlan)"));
        assert!(header.contains("offsetof(ReinstallPlan, should_fire_program_start)"));
        assert!(header.contains("static_assert(sizeof(ReinstallStep)"));
        assert!(header.contains("offsetof(ReinstallSequence, len)"));
        assert!(header.contains("static_assert(sizeof(ModStartupSequence)"));
        assert!(header.contains("offsetof(UnrealConfigPlan, engine_version_override_invalid)"));
    }
}
