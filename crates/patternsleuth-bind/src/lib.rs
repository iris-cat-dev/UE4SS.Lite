#![allow(unused)]

use std::{error::Error, ffi::c_void, panic, ptr, sync::Arc, time::Instant};

use patternsleuth::resolvers::{
    futures::join,
    impl_collector,
    unreal::{
        blueprint_library::UFunctionBind,
        engine_version::EngineVersion,
        fname::{FNameCtorWchar, FNameToString},
        ftext::FTextFString,
        fuobject_hash_tables::FUObjectHashTablesGet,
        game_loop::UGameEngineTick,
        gmalloc::GMalloc,
        guobject_array::GUObjectArray,
        kismet::GNatives,
        static_construct_object::StaticConstructObjectInternal,
        ConsoleManagerSingleton,
    },
    ResolveError,
};
use patternsleuth::scanner::Pattern;
use ue4ssl_abi::{PsCtx, PsScanResults};

impl_collector! {
    #[derive(Debug, PartialEq)]
    struct UE4SSResolution {
        guobject_array: GUObjectArray,
        fname_tostring: FNameToString,
        fname_ctor_wchar: FNameCtorWchar,
        gmalloc: GMalloc,
        static_construct_object_internal: StaticConstructObjectInternal,
        ftext_fstring: FTextFString,
        engine_version: EngineVersion,
        ufunction_bind: UFunctionBind,
        fuobject_hash_tables_get: FUObjectHashTablesGet,
        gnatives: GNatives,
        console_manager_singleton: ConsoleManagerSingleton,
        gameengine_tick: UGameEngineTick,
    }
}

fn log_with(sink: ue4ssl_abi::PsLogFn, msg: impl AsRef<str>) {
    let wchar: Vec<u16> = msg.as_ref().encode_utf16().chain([0]).collect();
    sink(wchar.as_ptr());
}

macro_rules! _log_level {
    ($level:ident, $ctx:ident) => { log_with($ctx.$level, "") };
    ($level:ident, $ctx:ident, $($arg:tt)*) => { log_with($ctx.$level, format!($($arg)*)) };
}
macro_rules! default { ($ctx:ident $($arg:tt)*) => { _log_level!(default_fn, $ctx $($arg)*) }; }
macro_rules! normal { ($ctx:ident $($arg:tt)*) => { _log_level!(normal_fn, $ctx $($arg)*) }; }
macro_rules! verbose { ($ctx:ident $($arg:tt)*) => { _log_level!(verbose_fn, $ctx $($arg)*) }; }
macro_rules! warning { ($ctx:ident $($arg:tt)*) => { _log_level!(warning_fn, $ctx $($arg)*) }; }
macro_rules! error { ($ctx:ident $($arg:tt)*) => { _log_level!(error_fn, $ctx $($arg)*) }; }

#[derive(Debug, Default)]
struct ScanErrors(Vec<Box<dyn Error>>);
impl std::error::Error for ScanErrors {}
impl std::fmt::Display for ScanErrors {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "ScanError")
    }
}

pub fn ps_scan_internal(ctx: &PsCtx, results: &mut PsScanResults) -> Result<(), Box<dyn Error>> {
    default!(ctx, "Reading image");

    let exe = patternsleuth::process::internal::read_image()?;

    default!(ctx, "Starting scan");
    let before = Instant::now();
    let resolution = exe.resolve(UE4SSResolution::resolver())?;
    default!(ctx, "Scan finished in {:?}", before.elapsed());

    let mut errors = ScanErrors::default();

    macro_rules! handle {
        ($member:ident, $name:literal, $lua:literal) => {
            if ctx.config.$member != 0 {
                match resolution.$member {
                    Ok(res) => {
                        default!(ctx, "Found {}: 0x{:x?}", $name, res.0);
                        results.$member = res.0 as usize;
                    }
                    Err(err) => {
                        warning!(ctx, "Failed to find {}: {err}", $name);
                        warning!(
                            ctx,
                            "You can supply your own AOB in 'UE4SS_Signatures/{}'",
                            $lua
                        );
                        errors.0.push(Box::new(err));
                    }
                }
            }
        };
    }
    if ctx.config.engine_version != 0 {
        match resolution.engine_version {
            Ok(res) => {
                default!(ctx, "Found EngineVersion: {}", res);
                results.engine_version.major = res.major;
                results.engine_version.minor = res.minor;
            }
            Err(err) => {
                warning!(ctx, "Failed to find EngineVersion: {err}");
                warning!(
                    ctx,
                    "You need to override the engine version in 'UE4SS-settings.ini'."
                );
                errors.0.push(Box::new(err));
            }
        }
    }
    macro_rules! unsupported {
        ($member:ident, $name:literal) => {
            if ctx.config.$member != 0 {
                warning!(
                    ctx,
                    "{} is not supported by the PatternSleuth Rust binder yet; use the legacy signature scanner or a config override.",
                    $name
                );
                errors.0.push(Box::new(ResolveError::new_msg(format!(
                    "{} is not supported by patternsleuth_bind",
                    $name
                ))));
            }
        };
    }
    handle!(guobject_array, "GUObjectArray", "GUObjectArray.lua");
    handle!(gmalloc, "GMalloc", "GMalloc.lua");
    handle!(fname_tostring, "FName::ToString", "FName_ToString.lua");
    handle!(
        fname_ctor_wchar,
        "FName::FName(wchar_t*)",
        "FName_Constructor.lua"
    );
    handle!(
        static_construct_object_internal,
        "StaticConstructObject_Internal",
        "StaticConstructObject.lua"
    );
    handle!(
        ftext_fstring,
        "FText::FText(FString&&)",
        "FText_Constructor.lua"
    );
    handle!(ufunction_bind, "UFunction::Bind", "UFunctionBind.lua");
    handle!(
        fuobject_hash_tables_get,
        "FUObjectHashTables::Get",
        "FUObjectHashTables.lua"
    );
    handle!(gnatives, "GNatives", "GNatives.lua");
    handle!(
        console_manager_singleton,
        "ConsoleManager::Singleton",
        "ConsoleManager.lua"
    );
    handle!(gameengine_tick, "GameEngine::Tick", "GameEngine_Tick.lua");

    if errors.0.is_empty() {
        Ok(())
    } else {
        Err(Box::new(errors))
    }
}

#[no_mangle]
pub extern "C" fn ps_scan(ctx: &PsCtx, results: &mut PsScanResults) -> bool {
    if let Err(_err) = ps_scan_internal(ctx, results) {
        warning!(ctx, "Scan failed\n");
        false
    } else {
        true
    }
}

type PsAobMatchCallback =
    unsafe extern "C" fn(address: usize, pattern_len: usize, user_data: *mut c_void) -> u8;

#[cfg(windows)]
#[repr(C)]
#[allow(non_snake_case)]
struct MemoryBasicInformation {
    BaseAddress: *mut c_void,
    AllocationBase: *mut c_void,
    AllocationProtect: u32,
    PartitionId: u16,
    RegionSize: usize,
    State: u32,
    Protect: u32,
    Type: u32,
}

#[cfg(windows)]
extern "system" {
    fn VirtualQuery(
        lp_address: *const c_void,
        lp_buffer: *mut MemoryBasicInformation,
        dw_length: usize,
    ) -> usize;
}

#[cfg(windows)]
const MEM_COMMIT: u32 = 0x1000;
#[cfg(windows)]
const PAGE_NOACCESS: u32 = 0x01;
#[cfg(windows)]
const PAGE_GUARD: u32 = 0x100;
#[cfg(windows)]
const PAGE_NOCACHE: u32 = 0x200;

fn pattern_from_ffi(pattern: *const u8, pattern_len: usize) -> Result<Pattern, String> {
    if pattern.is_null() || pattern_len == 0 {
        return Err("empty pattern".to_string());
    }

    let bytes = unsafe { std::slice::from_raw_parts(pattern, pattern_len) };
    let pattern = std::str::from_utf8(bytes).map_err(|err| err.to_string())?;
    Pattern::new(pattern).map_err(|err| err.to_string())
}

fn scan_aob_region(
    pattern: &Pattern,
    base_address: usize,
    data: &[u8],
    callback: PsAobMatchCallback,
    user_data: *mut c_void,
) -> (usize, bool) {
    let pattern_len = pattern.simple.len();
    if pattern_len == 0 || data.len() < pattern_len {
        return (0, false);
    }

    let mut matches = 0;
    for index in 0..=data.len() - pattern_len {
        if !pattern.is_match(data, base_address, index) {
            continue;
        }

        matches += 1;
        let address = pattern.compute_result(data, base_address, index);
        let should_stop = unsafe { callback(address, pattern_len, user_data) } != 0;
        if should_stop {
            return (matches, true);
        }
    }

    (matches, false)
}

fn scan_wide_region(needle: &[u16], base_address: usize, data: &[u8]) -> Option<usize> {
    if needle.is_empty() {
        return None;
    }

    let mut needle_bytes = Vec::with_capacity(needle.len() * 2);
    for unit in needle {
        needle_bytes.extend(unit.to_le_bytes());
    }

    if data.len() < needle_bytes.len() {
        return None;
    }

    data.windows(needle_bytes.len())
        .position(|window| window == needle_bytes.as_slice())
        .map(|offset| base_address + offset)
}

#[cfg(windows)]
unsafe fn for_each_readable_region(
    base: *const u8,
    size: usize,
    mut visit: impl FnMut(usize, &[u8]) -> bool,
) {
    let Some(end) = (base as usize).checked_add(size) else {
        return;
    };
    let mut current = base as usize;

    while current < end {
        let mut info = std::mem::zeroed::<MemoryBasicInformation>();
        let queried = VirtualQuery(
            current as *const c_void,
            &mut info,
            std::mem::size_of::<MemoryBasicInformation>(),
        );
        if queried == 0 || info.RegionSize == 0 {
            break;
        }

        let region_start = info.BaseAddress as usize;
        let region_end = region_start.saturating_add(info.RegionSize);
        let scan_start = current.max(region_start);
        let scan_end = end.min(region_end);
        let is_readable = info.State == MEM_COMMIT
            && (info.Protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_NOACCESS)) == 0;

        if is_readable && scan_start < scan_end {
            let data = std::slice::from_raw_parts(
                scan_start as *const u8,
                scan_end.saturating_sub(scan_start),
            );
            if visit(scan_start, data) {
                break;
            }
        }

        current = region_end.max(current.saturating_add(1));
    }
}

#[cfg(not(windows))]
unsafe fn for_each_readable_region(
    base: *const u8,
    size: usize,
    mut visit: impl FnMut(usize, &[u8]) -> bool,
) {
    if base.is_null() || size == 0 {
        return;
    }

    let data = std::slice::from_raw_parts(base, size);
    let _ = visit(base as usize, data);
}

#[no_mangle]
pub extern "C" fn ps_scan_aob(
    base: *const u8,
    size: usize,
    pattern: *const u8,
    pattern_len: usize,
    callback: Option<PsAobMatchCallback>,
    user_data: *mut c_void,
) -> usize {
    if base.is_null() || size == 0 {
        return 0;
    }

    let Some(callback) = callback else {
        return usize::MAX;
    };

    let Ok(pattern) = pattern_from_ffi(pattern, pattern_len) else {
        return usize::MAX;
    };

    let result = panic::catch_unwind(panic::AssertUnwindSafe(|| {
        let mut total_matches = 0;
        unsafe {
            for_each_readable_region(base, size, |region_base, data| {
                let (matches, stopped) =
                    scan_aob_region(&pattern, region_base, data, callback, user_data);
                total_matches += matches;
                stopped
            });
        }
        total_matches
    }));

    result.unwrap_or(usize::MAX)
}

#[no_mangle]
pub extern "C" fn ps_scan_wide_string(
    base: *const u8,
    size: usize,
    needle: *const u16,
    needle_len: usize,
) -> usize {
    if base.is_null() || size == 0 || needle.is_null() || needle_len == 0 {
        return 0;
    }

    let needle = unsafe { std::slice::from_raw_parts(needle, needle_len) };
    let result = panic::catch_unwind(panic::AssertUnwindSafe(|| {
        let mut found = None;
        unsafe {
            for_each_readable_region(base, size, |region_base, data| {
                found = scan_wide_region(needle, region_base, data);
                found.is_some()
            });
        }
        found.unwrap_or(0)
    }));

    result.unwrap_or(0)
}

#[inline(never)]
pub fn force_link_exports() {
    ue4ssl_hook::force_link_exports();
    #[cfg(not(test))]
    ue4ssl_runtime::force_link_exports();
    let _ = ps_scan as *const () as usize;
    let _ = ps_scan_aob as *const () as usize;
    let _ = ps_scan_wide_string as *const () as usize;
}
