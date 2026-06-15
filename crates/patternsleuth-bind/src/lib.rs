#![allow(unused)]

use std::{error::Error, sync::Arc, time::Instant};

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

#[inline(never)]
pub fn force_link_exports() {
    ue4ssl_hook::force_link_exports();
    #[cfg(not(test))]
    ue4ssl_core::force_link_exports();
    ue4ssl_host::force_link_exports();
    let _ = ps_scan as *const () as usize;
}
