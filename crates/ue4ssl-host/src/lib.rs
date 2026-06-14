use std::collections::HashSet;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::slice;
use std::sync::{Mutex, OnceLock};

use ue4ssl_abi::{
    HostModStartupSequence, HostModStartupStep, HostReinstallPlan, HostReinstallSequence,
    HostReinstallStep, HostUnrealConfigPlan, SliceU16,
};

const ACTION_RESET_DLL_DISPATCH_CACHE: u32 = 1;
const ACTION_SET_PAUSE_PROCESSING: u32 = 2;
const ACTION_UNINSTALL_MODS: u32 = 3;
const ACTION_REMOVE_SCRIPT_KEYBINDS: u32 = 4;
const ACTION_SETUP_MODS: u32 = 5;
const ACTION_START_CPP_MODS: u32 = 6;
const ACTION_FIRE_UNREAL_INIT: u32 = 7;
const ACTION_FIRE_PROGRAM_START: u32 = 8;

const STARTUP_ACTION_START_NAMED_MOD: u32 = 1;
const STARTUP_ACTION_START_DISCOVERED_MODS: u32 = 2;

const UE4SSL_CSHARP: &[u16] = &[
    b'U' as u16,
    b'E' as u16,
    b'4' as u16,
    b'S' as u16,
    b'S' as u16,
    b'L' as u16,
    b'.' as u16,
    b'C' as u16,
    b'S' as u16,
    b'h' as u16,
    b'a' as u16,
    b'r' as u16,
    b'p' as u16,
];
const UE4SSL_JAVASCRIPT: &[u16] = &[
    b'U' as u16,
    b'E' as u16,
    b'4' as u16,
    b'S' as u16,
    b'S' as u16,
    b'L' as u16,
    b'.' as u16,
    b'J' as u16,
    b'a' as u16,
    b'v' as u16,
    b'a' as u16,
    b'S' as u16,
    b'c' as u16,
    b'r' as u16,
    b'i' as u16,
    b'p' as u16,
    b't' as u16,
];
const UE4SSL_LUA: &[u16] = &[
    b'U' as u16,
    b'E' as u16,
    b'4' as u16,
    b'S' as u16,
    b'S' as u16,
    b'L' as u16,
    b'.' as u16,
    b'L' as u16,
    b'u' as u16,
    b'a' as u16,
];
const UE4SSL_DYNAMIC_LOAD_PAK: &[u16] = &[
    b'U' as u16,
    b'E' as u16,
    b'4' as u16,
    b'S' as u16,
    b'S' as u16,
    b'L' as u16,
    b'.' as u16,
    b'D' as u16,
    b'y' as u16,
    b'n' as u16,
    b'a' as u16,
    b'm' as u16,
    b'i' as u16,
    b'c' as u16,
    b'L' as u16,
    b'o' as u16,
    b'a' as u16,
    b'd' as u16,
    b'P' as u16,
    b'a' as u16,
    b'k' as u16,
];
const UE4SSL_DRG: &[u16] = &[
    b'U' as u16,
    b'E' as u16,
    b'4' as u16,
    b'S' as u16,
    b'S' as u16,
    b'L' as u16,
    b'.' as u16,
    b'D' as u16,
    b'R' as u16,
    b'G' as u16,
];

fn dll_dispatch_cache() -> &'static Mutex<HashSet<String>> {
    static DLL_DISPATCH_CACHE: OnceLock<Mutex<HashSet<String>>> = OnceLock::new();
    DLL_DISPATCH_CACHE.get_or_init(|| Mutex::new(HashSet::new()))
}

fn slice_to_string(slice: SliceU16) -> String {
    if slice.data.is_null() || slice.len == 0 {
        return String::new();
    }

    let utf16 = unsafe { slice::from_raw_parts(slice.data, slice.len) };
    String::from_utf16_lossy(utf16)
}

#[no_mangle]
pub extern "C" fn ue4ssl_host_plan_reinstall(
    is_unreal_initialized: u8,
    is_program_started: u8,
) -> HostReinstallPlan {
    HostReinstallPlan {
        should_pause_processing: 1,
        should_resume_before_restart: 1,
        should_fire_unreal_init: u8::from(is_unreal_initialized != 0),
        should_fire_program_start: u8::from(is_program_started != 0),
    }
}

fn push_step(sequence: &mut HostReinstallSequence, action: u32, value: u8) {
    if sequence.len < sequence.steps.len() {
        sequence.steps[sequence.len] = HostReinstallStep { action, value };
        sequence.len += 1;
    }
}

fn push_startup_step(sequence: &mut HostModStartupSequence, action: u32, mod_name: &'static [u16]) {
    if sequence.len < sequence.steps.len() {
        sequence.steps[sequence.len] = HostModStartupStep {
            action,
            mod_name: mod_name.as_ptr(),
            mod_name_len: mod_name.len(),
        };
        sequence.len += 1;
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_host_plan_reinstall_sequence(
    is_unreal_initialized: u8,
    is_program_started: u8,
) -> HostReinstallSequence {
    catch_unwind(AssertUnwindSafe(|| {
        let plan = ue4ssl_host_plan_reinstall(is_unreal_initialized, is_program_started);
        let mut sequence = HostReinstallSequence::default();

        push_step(&mut sequence, ACTION_RESET_DLL_DISPATCH_CACHE, 0);
        push_step(
            &mut sequence,
            ACTION_SET_PAUSE_PROCESSING,
            plan.should_pause_processing,
        );
        push_step(&mut sequence, ACTION_UNINSTALL_MODS, 0);
        push_step(&mut sequence, ACTION_REMOVE_SCRIPT_KEYBINDS, 0);
        push_step(
            &mut sequence,
            ACTION_SET_PAUSE_PROCESSING,
            u8::from(plan.should_resume_before_restart == 0),
        );
        push_step(&mut sequence, ACTION_SETUP_MODS, 0);
        push_step(&mut sequence, ACTION_START_CPP_MODS, 0);

        if plan.should_resume_before_restart != 0 {
            push_step(&mut sequence, ACTION_SET_PAUSE_PROCESSING, 0);
        }
        if plan.should_fire_unreal_init != 0 {
            push_step(&mut sequence, ACTION_FIRE_UNREAL_INIT, 0);
        }
        if plan.should_fire_program_start != 0 {
            push_step(&mut sequence, ACTION_FIRE_PROGRAM_START, 0);
        }

        sequence
    }))
    .unwrap_or_default()
}

#[no_mangle]
pub extern "C" fn ue4ssl_host_plan_mod_startup_sequence() -> HostModStartupSequence {
    catch_unwind(AssertUnwindSafe(|| {
        let mut sequence = HostModStartupSequence::default();
        push_startup_step(&mut sequence, STARTUP_ACTION_START_NAMED_MOD, UE4SSL_CSHARP);
        push_startup_step(
            &mut sequence,
            STARTUP_ACTION_START_NAMED_MOD,
            UE4SSL_JAVASCRIPT,
        );
        push_startup_step(&mut sequence, STARTUP_ACTION_START_NAMED_MOD, UE4SSL_LUA);
        push_startup_step(
            &mut sequence,
            STARTUP_ACTION_START_NAMED_MOD,
            UE4SSL_DYNAMIC_LOAD_PAK,
        );
        push_startup_step(&mut sequence, STARTUP_ACTION_START_NAMED_MOD, UE4SSL_DRG);
        push_startup_step(&mut sequence, STARTUP_ACTION_START_DISCOVERED_MODS, &[]);
        sequence
    }))
    .unwrap_or_default()
}

#[no_mangle]
pub extern "C" fn ue4ssl_host_should_dispatch_dll_load(dll_name: SliceU16) -> u8 {
    catch_unwind(AssertUnwindSafe(|| {
        let dll_name = slice_to_string(dll_name);
        if dll_name.is_empty() {
            return 0;
        }

        let normalized = dll_name.to_lowercase();
        match dll_dispatch_cache().lock() {
            Ok(mut cache) => u8::from(cache.insert(normalized)),
            Err(poisoned) => {
                let mut cache = poisoned.into_inner();
                u8::from(cache.insert(normalized))
            }
        }
    }))
    .unwrap_or(1)
}

fn is_valid_u32(value: i64) -> bool {
    value >= 0 && value <= u32::MAX as i64
}

#[no_mangle]
pub extern "C" fn ue4ssl_host_plan_unreal_config(
    sig_scanner_num_threads: i64,
    sig_scanner_multithreading_module_size_threshold: i64,
    engine_version_major: i64,
    engine_version_minor: i64,
    fexec_vtable_offset_in_local_player: i64,
) -> HostUnrealConfigPlan {
    catch_unwind(AssertUnwindSafe(|| {
        let mut plan = HostUnrealConfigPlan {
            fexec_vtable_offset_in_local_player: fexec_vtable_offset_in_local_player as u8,
            is_fexec_vtable_offset_in_local_player_in_range: u8::from(
                (0..=u8::MAX as i64).contains(&fexec_vtable_offset_in_local_player),
            ),
            ..HostUnrealConfigPlan::default()
        };

        if sig_scanner_num_threads >= 1 && sig_scanner_num_threads <= u32::MAX as i64 {
            plan.has_num_scan_threads = 1;
            plan.num_scan_threads = sig_scanner_num_threads as u32;
        }

        if is_valid_u32(sig_scanner_multithreading_module_size_threshold) {
            plan.has_multithreading_module_size_threshold = 1;
            plan.multithreading_module_size_threshold =
                sig_scanner_multithreading_module_size_threshold as u32;
        }

        if engine_version_major != -1 && engine_version_minor != -1 {
            if !is_valid_u32(engine_version_major) || !is_valid_u32(engine_version_minor) {
                plan.engine_version_override_invalid = 1;
            } else {
                plan.has_engine_version_override = 1;
                plan.engine_version_major = engine_version_major as u32;
                plan.engine_version_minor = engine_version_minor as u32;
            }
        }

        plan.should_enable_builtin_guobjectarray_fallback =
            u8::from(engine_version_major == 5 && engine_version_minor >= 6);

        plan
    }))
    .unwrap_or_default()
}

#[no_mangle]
pub extern "C" fn ue4ssl_host_reset_dll_dispatch_cache() {
    let _ = catch_unwind(AssertUnwindSafe(|| match dll_dispatch_cache().lock() {
        Ok(mut cache) => cache.clear(),
        Err(poisoned) => {
            let mut cache = poisoned.into_inner();
            cache.clear();
        }
    }));
}

#[inline(never)]
pub fn force_link_exports() {
    let _ = ue4ssl_host_plan_reinstall as *const () as usize;
    let _ = ue4ssl_host_plan_reinstall_sequence as *const () as usize;
    let _ = ue4ssl_host_plan_mod_startup_sequence as *const () as usize;
    let _ = ue4ssl_host_should_dispatch_dll_load as *const () as usize;
    let _ = ue4ssl_host_plan_unreal_config as *const () as usize;
    let _ = ue4ssl_host_reset_dll_dispatch_cache as *const () as usize;
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Mutex, OnceLock};

    fn test_lock() -> std::sync::MutexGuard<'static, ()> {
        static LOCK: OnceLock<Mutex<()>> = OnceLock::new();
        LOCK.get_or_init(|| Mutex::new(())).lock().unwrap()
    }

    fn slice_from_str(value: &str) -> (Vec<u16>, SliceU16) {
        let wide: Vec<u16> = value.encode_utf16().collect();
        let slice = SliceU16 {
            data: wide.as_ptr(),
            len: wide.len(),
        };
        (wide, slice)
    }

    fn startup_name(step: HostModStartupStep) -> String {
        let slice = unsafe { slice::from_raw_parts(step.mod_name, step.mod_name_len) };
        String::from_utf16_lossy(slice)
    }

    #[test]
    fn startup_sequence_keeps_builtin_order_before_discovered_mods() {
        let sequence = ue4ssl_host_plan_mod_startup_sequence();

        assert_eq!(sequence.len, 6);
        assert_eq!(sequence.steps[0].action, STARTUP_ACTION_START_NAMED_MOD);
        assert_eq!(startup_name(sequence.steps[0]), "UE4SSL.CSharp");
        assert_eq!(startup_name(sequence.steps[1]), "UE4SSL.JavaScript");
        assert_eq!(startup_name(sequence.steps[2]), "UE4SSL.Lua");
        assert_eq!(startup_name(sequence.steps[3]), "UE4SSL.DynamicLoadPak");
        assert_eq!(startup_name(sequence.steps[4]), "UE4SSL.DRG");
        assert_eq!(
            sequence.steps[5].action,
            STARTUP_ACTION_START_DISCOVERED_MODS
        );
    }

    #[test]
    fn dll_dispatch_deduplicates_case_insensitive_names_until_reset() {
        let _guard = test_lock();
        ue4ssl_host_reset_dll_dispatch_cache();
        let (_first_buf, first) = slice_from_str("Foo.dll");
        let (_second_buf, second) = slice_from_str("foo.DLL");

        assert_eq!(ue4ssl_host_should_dispatch_dll_load(first), 1);
        assert_eq!(ue4ssl_host_should_dispatch_dll_load(second), 0);

        ue4ssl_host_reset_dll_dispatch_cache();
        let (_third_buf, third) = slice_from_str("foo.dll");
        assert_eq!(ue4ssl_host_should_dispatch_dll_load(third), 1);
    }

    #[test]
    fn dll_dispatch_ignores_empty_names() {
        let _guard = test_lock();
        ue4ssl_host_reset_dll_dispatch_cache();

        assert_eq!(ue4ssl_host_should_dispatch_dll_load(SliceU16::default()), 0);
    }

    #[test]
    fn unreal_config_plan_preserves_valid_overrides() {
        let plan = ue4ssl_host_plan_unreal_config(4, 1024, 5, 6, 0x28);

        assert_eq!(plan.has_num_scan_threads, 1);
        assert_eq!(plan.num_scan_threads, 4);
        assert_eq!(plan.has_multithreading_module_size_threshold, 1);
        assert_eq!(plan.multithreading_module_size_threshold, 1024);
        assert_eq!(plan.has_engine_version_override, 1);
        assert_eq!(plan.engine_version_override_invalid, 0);
        assert_eq!(plan.engine_version_major, 5);
        assert_eq!(plan.engine_version_minor, 6);
        assert_eq!(plan.should_enable_builtin_guobjectarray_fallback, 1);
        assert_eq!(plan.fexec_vtable_offset_in_local_player, 0x28);
        assert_eq!(plan.is_fexec_vtable_offset_in_local_player_in_range, 1);
    }

    #[test]
    fn unreal_config_plan_rejects_invalid_ranges_without_overrides() {
        let plan = ue4ssl_host_plan_unreal_config(0, -1, i64::from(u32::MAX) + 1, 0, 300);

        assert_eq!(plan.has_num_scan_threads, 0);
        assert_eq!(plan.has_multithreading_module_size_threshold, 0);
        assert_eq!(plan.has_engine_version_override, 0);
        assert_eq!(plan.engine_version_override_invalid, 1);
        assert_eq!(plan.fexec_vtable_offset_in_local_player, 44);
        assert_eq!(plan.is_fexec_vtable_offset_in_local_player_in_range, 0);
    }
}
