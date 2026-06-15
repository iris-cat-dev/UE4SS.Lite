mod contract;
mod cpp_mod;
mod ini;
mod mods;
mod paths;
mod settings;
mod state;
mod wide;

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use cpp_mod::CppModHandle;
use mods::DiscoveredModSpec;
use paths::ProgramPathSnapshot;
use settings::SettingsSnapshot;
use ue4ssl_abi::{
    CppModRuntimeStatus, DiscoveredMod, ModDiscovery, OwnedString, PathSnapshot, ProgramFlags,
    SliceU16,
};

pub struct IniHandle {
    parsed: ini::ParsedIni,
}

fn ffi_or_default<T, F>(callable: F) -> T
where
    T: Default,
    F: FnOnce() -> T,
{
    catch_unwind(AssertUnwindSafe(callable)).unwrap_or_default()
}

impl From<ProgramPathSnapshot> for PathSnapshot {
    fn from(value: ProgramPathSnapshot) -> Self {
        Self {
            root_directory: wide::path_to_owned_string(&value.root_directory),
            working_directory: wide::path_to_owned_string(&value.working_directory),
            mods_directory: wide::path_to_owned_string(&value.mods_directory),
            game_executable_directory: wide::path_to_owned_string(&value.game_executable_directory),
            settings_path_and_file: wide::path_to_owned_string(&value.settings_path_and_file),
            legacy_root_directory: wide::path_to_owned_string(&value.legacy_root_directory),
            object_dumper_output_directory: wide::path_to_owned_string(
                &value.object_dumper_output_directory,
            ),
            log_directory: wide::path_to_owned_string(&value.log_directory),
            game_path_and_exe_name: wide::path_to_owned_string(&value.game_path_and_exe_name),
            has_game_specific_config: u8::from(value.has_game_specific_config),
        }
    }
}

impl From<DiscoveredModSpec> for DiscoveredMod {
    fn from(value: DiscoveredModSpec) -> Self {
        let has_custom_dll_name = value.dll_name.is_some();
        let dll_name = value
            .dll_name
            .as_ref()
            .map(|name| wide::os_str_to_owned_string(name))
            .unwrap_or_default();

        Self {
            mod_name: wide::os_str_to_owned_string(&value.mod_name),
            mod_path: wide::path_to_owned_string(&value.mod_path),
            dll_name,
            has_custom_dll_name: u8::from(has_custom_dll_name),
            is_builtin: u8::from(value.is_builtin),
        }
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_compute_base_paths(
    module_file_path: SliceU16,
    game_exe_path: SliceU16,
) -> PathSnapshot {
    ffi_or_default(|| {
        let module_file_path = wide::slice_to_path_buf(module_file_path);
        let game_exe_path = wide::slice_to_path_buf(game_exe_path);
        paths::compute_base_paths(&module_file_path, &game_exe_path).into()
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_resolve_mods_directory(
    working_directory: SliceU16,
    current_mods_directory: SliceU16,
    override_mods_directory: SliceU16,
) -> OwnedString {
    ffi_or_default(|| {
        let working_directory = wide::slice_to_path_buf(working_directory);
        let current_mods_directory = wide::slice_to_path_buf(current_mods_directory);
        let override_mods_directory = wide::slice_to_path_buf(override_mods_directory);
        let resolved = if override_mods_directory.components().next().is_none() {
            paths::resolve_mods_directory(&working_directory, &current_mods_directory, None)
        } else {
            paths::resolve_mods_directory(
                &working_directory,
                &current_mods_directory,
                Some(&override_mods_directory),
            )
        };

        wide::path_to_owned_string(&resolved)
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_discover_mods(
    working_directory: SliceU16,
    mods_directory: SliceU16,
) -> ModDiscovery {
    ffi_or_default(|| {
        let working_directory = wide::slice_to_path_buf(working_directory);
        let mods_directory = wide::slice_to_path_buf(mods_directory);
        let discovered_mods = mods::discover_mods(&working_directory, &mods_directory);
        let ffi_mods: Vec<DiscoveredMod> = discovered_mods
            .into_iter()
            .map(DiscoveredMod::from)
            .collect();

        if ffi_mods.is_empty() {
            return ModDiscovery::default();
        }

        let mut boxed = ffi_mods.into_boxed_slice();
        let mods = boxed.as_mut_ptr();
        let len = boxed.len();
        std::mem::forget(boxed);

        ModDiscovery { mods, len }
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_load_settings(settings_path: SliceU16) -> SettingsSnapshot {
    ffi_or_default(|| {
        let settings_path = wide::slice_to_path_buf(settings_path);
        settings::load_settings(&settings_path)
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_ini_parse(contents: SliceU16) -> *mut IniHandle {
    ffi_or_default(|| {
        let contents = wide::slice_to_string_lossy(contents);
        Box::into_raw(Box::new(IniHandle {
            parsed: ini::ParsedIni::parse(&contents),
        }))
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_ini_destroy(handle: *mut IniHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !handle.is_null() {
            drop(Box::from_raw(handle));
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_ini_get_i64(
    handle: *const IniHandle,
    section: SliceU16,
    key: SliceU16,
    default_value: i64,
) -> i64 {
    ffi_or_default(|| unsafe {
        let Some(handle) = handle.as_ref() else {
            return default_value;
        };
        let section = wide::slice_to_string_lossy(section);
        let key = wide::slice_to_string_lossy(key);
        handle.parsed.get_i64(&section, &key, default_value)
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_ini_ordered_list_len(
    handle: *const IniHandle,
    section: SliceU16,
) -> usize {
    ffi_or_default(|| unsafe {
        let Some(handle) = handle.as_ref() else {
            return 0;
        };
        let section = wide::slice_to_string_lossy(section);
        handle.parsed.ordered_list_len(&section)
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_ini_ordered_list_item(
    handle: *const IniHandle,
    section: SliceU16,
    index: usize,
) -> OwnedString {
    ffi_or_default(|| unsafe {
        let Some(handle) = handle.as_ref() else {
            return OwnedString::default();
        };
        let section = wide::slice_to_string_lossy(section);
        handle
            .parsed
            .ordered_list_item(&section, index)
            .map(wide::str_to_owned_string)
            .unwrap_or_default()
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_get_program_flags() -> ProgramFlags {
    ffi_or_default(|| {
        let flags = state::get_program_flags();
        ProgramFlags {
            is_program_started: u8::from(flags.is_program_started),
            processing_events: u8::from(flags.processing_events),
            pause_events_processing: u8::from(flags.pause_events_processing),
        }
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_set_program_started(is_program_started: u8) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        state::set_program_started(is_program_started != 0);
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_set_processing_state(
    processing_events: u8,
    pause_events_processing: u8,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        state::set_processing_state(processing_events != 0, pause_events_processing != 0);
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_free_string(mut string: OwnedString) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        wide::free_owned_string_in_place(&mut string);
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_free_path_snapshot(mut snapshot: PathSnapshot) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        wide::free_owned_string_in_place(&mut snapshot.root_directory);
        wide::free_owned_string_in_place(&mut snapshot.working_directory);
        wide::free_owned_string_in_place(&mut snapshot.mods_directory);
        wide::free_owned_string_in_place(&mut snapshot.game_executable_directory);
        wide::free_owned_string_in_place(&mut snapshot.settings_path_and_file);
        wide::free_owned_string_in_place(&mut snapshot.legacy_root_directory);
        wide::free_owned_string_in_place(&mut snapshot.object_dumper_output_directory);
        wide::free_owned_string_in_place(&mut snapshot.log_directory);
        wide::free_owned_string_in_place(&mut snapshot.game_path_and_exe_name);
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_free_mod_discovery(discovery: ModDiscovery) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if discovery.mods.is_null() || discovery.len == 0 {
            return;
        }

        let raw = ptr::slice_from_raw_parts_mut(discovery.mods, discovery.len);
        let mut boxed = Box::from_raw(raw);
        for mod_spec in boxed.iter_mut() {
            wide::free_owned_string_in_place(&mut mod_spec.mod_name);
            wide::free_owned_string_in_place(&mut mod_spec.mod_path);
            wide::free_owned_string_in_place(&mut mod_spec.dll_name);
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_create(
    mod_path: SliceU16,
    dll_name: SliceU16,
) -> *mut CppModHandle {
    ffi_or_default(|| Box::into_raw(cpp_mod::CppModHandle::load(mod_path, dll_name)))
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_destroy(handle: *mut CppModHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !handle.is_null() {
            drop(Box::from_raw(handle));
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_status(handle: *const CppModHandle) -> CppModRuntimeStatus {
    ffi_or_default(|| unsafe {
        handle
            .as_ref()
            .map(CppModHandle::status)
            .unwrap_or_default()
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_set_installable(handle: *mut CppModHandle, value: u8) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if let Some(handle) = handle.as_mut() {
            handle.set_installable(value != 0);
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_set_installed(handle: *mut CppModHandle, value: u8) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if let Some(handle) = handle.as_mut() {
            handle.set_installed(value != 0);
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_set_updates_disabled(handle: *mut CppModHandle, value: u8) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if let Some(handle) = handle.as_mut() {
            handle.set_updates_disabled(value != 0);
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_start(handle: *mut CppModHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if let Some(handle) = handle.as_mut() {
            handle.start();
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_uninstall(handle: *mut CppModHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if let Some(handle) = handle.as_mut() {
            handle.uninstall();
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_fire_unreal_init(handle: *mut CppModHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if let Some(handle) = handle.as_mut() {
            handle.fire_unreal_init();
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_fire_ui_init(handle: *mut CppModHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if let Some(handle) = handle.as_mut() {
            handle.fire_ui_init();
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_fire_program_start(handle: *mut CppModHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if let Some(handle) = handle.as_mut() {
            handle.fire_program_start();
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_fire_update(handle: *mut CppModHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if let Some(handle) = handle.as_mut() {
            handle.fire_update();
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ssl_core_cppmod_fire_dll_load(handle: *mut CppModHandle, dll_name: SliceU16) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if let Some(handle) = handle.as_mut() {
            handle.fire_dll_load(dll_name);
        }
    }));
}

#[inline(never)]
pub fn force_link_exports() {
    let _ = ue4ssl_core_compute_base_paths as *const () as usize;
    let _ = ue4ssl_core_resolve_mods_directory as *const () as usize;
    let _ = ue4ssl_core_discover_mods as *const () as usize;
    let _ = ue4ssl_core_load_settings as *const () as usize;
    let _ = ue4ssl_core_ini_parse as *const () as usize;
    let _ = ue4ssl_core_ini_destroy as *const () as usize;
    let _ = ue4ssl_core_ini_get_i64 as *const () as usize;
    let _ = ue4ssl_core_ini_ordered_list_len as *const () as usize;
    let _ = ue4ssl_core_ini_ordered_list_item as *const () as usize;
    let _ = ue4ssl_core_get_program_flags as *const () as usize;
    let _ = ue4ssl_core_set_program_started as *const () as usize;
    let _ = ue4ssl_core_set_processing_state as *const () as usize;
    let _ = ue4ssl_core_free_string as *const () as usize;
    let _ = ue4ssl_core_free_path_snapshot as *const () as usize;
    let _ = ue4ssl_core_free_mod_discovery as *const () as usize;
    let _ = ue4ssl_core_cppmod_create as *const () as usize;
    let _ = ue4ssl_core_cppmod_destroy as *const () as usize;
    let _ = ue4ssl_core_cppmod_status as *const () as usize;
    let _ = ue4ssl_core_cppmod_set_installable as *const () as usize;
    let _ = ue4ssl_core_cppmod_set_installed as *const () as usize;
    let _ = ue4ssl_core_cppmod_set_updates_disabled as *const () as usize;
    let _ = ue4ssl_core_cppmod_start as *const () as usize;
    let _ = ue4ssl_core_cppmod_uninstall as *const () as usize;
    let _ = ue4ssl_core_cppmod_fire_unreal_init as *const () as usize;
    let _ = ue4ssl_core_cppmod_fire_ui_init as *const () as usize;
    let _ = ue4ssl_core_cppmod_fire_program_start as *const () as usize;
    let _ = ue4ssl_core_cppmod_fire_update as *const () as usize;
    let _ = ue4ssl_core_cppmod_fire_dll_load as *const () as usize;
}
