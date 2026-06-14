use std::ffi::c_void;
use std::os::raw::{c_char, c_int};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr::{self, NonNull};

fn package_marker() {
    ue4ssl_javascript_support::ue4ssl_javascript_support_anchor();
}

#[used]
static FORCE_LINK_JAVASCRIPT_PACKAGE: fn() = package_marker;

#[repr(C)]
pub struct JsEngine {
    inner: NonNull<c_void>,
}

impl JsEngine {
    fn create() -> Option<Box<Self>> {
        // The C++ side still owns the UE4SS class ABI and SEH boundary.
        let raw = unsafe { ue4ssl_js_cpp_create() };
        NonNull::new(raw).map(|inner| Box::new(Self { inner }))
    }

    fn init_engine(&mut self) -> c_int {
        unsafe { ue4ssl_js_cpp_init_engine(self.inner.as_ptr()) }
    }

    fn load_scripts(&mut self) -> c_int {
        unsafe { ue4ssl_js_cpp_load_scripts(self.inner.as_ptr()) }
    }

    fn tick(&mut self) -> c_int {
        unsafe { ue4ssl_js_cpp_tick(self.inner.as_ptr()) }
    }

    fn is_initialized(&self) -> bool {
        unsafe { ue4ssl_js_cpp_is_initialized(self.inner.as_ptr()) }
    }

    fn eval(&mut self, code: *const c_char, code_len: usize, filename: *const c_char) -> bool {
        unsafe { ue4ssl_js_cpp_eval(self.inner.as_ptr(), code, code_len, filename) }
    }
}

impl Drop for JsEngine {
    fn drop(&mut self) {
        unsafe { ue4ssl_js_cpp_destroy(self.inner.as_ptr()) };
    }
}

#[repr(C)]
struct GlobalApiEntry {
    name: &'static [u8],
    arity: c_int,
}

const GLOBAL_API: &[GlobalApiEntry] = &[
    GlobalApiEntry {
        name: b"print\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"FindFirstOf\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"FindAllOf\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"FindAllActorsWithInterface\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"StaticFindObject\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"LoadObject\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"ScanBlueprintWidgetsByInterface\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"RegisterHook\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"RegisterBindHook\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"RegisterNativeObjectMethodHook\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"RegisterLoadMapPreHook\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"RegisterLoadMapPostHook\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"HookUFunction\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"UnregisterHook\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"UnregisterBindHook\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"UnregisterLoadMapHook\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"NotifyOnNewObject\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"RegisterKeyBind\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"CallFunction\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"CallFunctionEx\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"__withExecBudget\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"setTimeout\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"setInterval\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"clearTimeout\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"clearInterval\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"fetch\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"fetchSync\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"SigScan\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"PatchByte\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"ReadByte\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"readFile\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"writeFile\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"getModsDirectory\0",
        arity: 0,
    },
    GlobalApiEntry {
        name: b"getGameDirectory\0",
        arity: 0,
    },
    GlobalApiEntry {
        name: b"downloadFile\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"downloadFileSync\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"playSoundFile\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"stopSound\0",
        arity: 0,
    },
    GlobalApiEntry {
        name: b"GetProperty\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"SetProperty\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"ExportPropertyText\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"GetPropertyPath\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"SetPropertyPath\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"ApplyObjectPatch\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"BindDelegate\0",
        arity: 4,
    },
    GlobalApiEntry {
        name: b"UnbindDelegate\0",
        arity: 4,
    },
    GlobalApiEntry {
        name: b"ClearDelegate\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"RegisterProcessEventWatch\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"BindDelegateCallback\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"UnbindDelegateCallback\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"NewUObject\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"__umgDispatchSync\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"__umgDispatchAsync\0",
        arity: 1,
    },
    GlobalApiEntry {
        name: b"__umgCreateUserWidget\0",
        arity: 3,
    },
    GlobalApiEntry {
        name: b"__umgCloneUserWidget\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"__umgSetUserWidgetRoot\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"__umgConstructWidget\0",
        arity: 2,
    },
    GlobalApiEntry {
        name: b"UE4SS\0",
        arity: -1,
    },
];

#[cfg(not(test))]
extern "C" {
    fn ue4ssl_js_cpp_create() -> *mut c_void;
    fn ue4ssl_js_cpp_destroy(engine: *mut c_void);
    fn ue4ssl_js_cpp_init_engine(engine: *mut c_void) -> c_int;
    fn ue4ssl_js_cpp_load_scripts(engine: *mut c_void) -> c_int;
    fn ue4ssl_js_cpp_tick(engine: *mut c_void) -> c_int;
    fn ue4ssl_js_cpp_is_initialized(engine: *mut c_void) -> bool;
    fn ue4ssl_js_cpp_eval(
        engine: *mut c_void,
        code: *const c_char,
        code_len: usize,
        filename: *const c_char,
    ) -> bool;
}

#[cfg(test)]
unsafe fn ue4ssl_js_cpp_create() -> *mut c_void {
    ptr::null_mut()
}

#[cfg(test)]
unsafe fn ue4ssl_js_cpp_destroy(_engine: *mut c_void) {}

#[cfg(test)]
unsafe fn ue4ssl_js_cpp_init_engine(_engine: *mut c_void) -> c_int {
    0
}

#[cfg(test)]
unsafe fn ue4ssl_js_cpp_load_scripts(_engine: *mut c_void) -> c_int {
    0
}

#[cfg(test)]
unsafe fn ue4ssl_js_cpp_tick(_engine: *mut c_void) -> c_int {
    0
}

#[cfg(test)]
unsafe fn ue4ssl_js_cpp_is_initialized(_engine: *mut c_void) -> bool {
    false
}

#[cfg(test)]
unsafe fn ue4ssl_js_cpp_eval(
    _engine: *mut c_void,
    _code: *const c_char,
    _code_len: usize,
    _filename: *const c_char,
) -> bool {
    false
}

fn with_engine_mut<T>(engine: *mut JsEngine, fallback: T, f: impl FnOnce(&mut JsEngine) -> T) -> T {
    if engine.is_null() {
        return fallback;
    }

    catch_unwind(AssertUnwindSafe(|| f(unsafe { &mut *engine }))).unwrap_or(fallback)
}

fn with_engine<T>(engine: *const JsEngine, fallback: T, f: impl FnOnce(&JsEngine) -> T) -> T {
    if engine.is_null() {
        return fallback;
    }

    catch_unwind(AssertUnwindSafe(|| f(unsafe { &*engine }))).unwrap_or(fallback)
}

#[no_mangle]
pub extern "C" fn ue4ssl_js_create() -> *mut JsEngine {
    catch_unwind(JsEngine::create)
        .ok()
        .flatten()
        .map(Box::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
///
/// `engine` must be null or a live pointer previously returned by `ue4ssl_js_create`.
#[no_mangle]
pub unsafe extern "C" fn ue4ssl_js_destroy(engine: *mut JsEngine) {
    if engine.is_null() {
        return;
    }

    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        drop(Box::from_raw(engine));
    }));
}

/// # Safety
///
/// `engine` must be null or a live pointer previously returned by `ue4ssl_js_create`.
#[no_mangle]
pub unsafe extern "C" fn ue4ssl_js_init_engine(engine: *mut JsEngine) -> c_int {
    with_engine_mut(engine, 0, JsEngine::init_engine)
}

/// # Safety
///
/// `engine` must be null or a live pointer previously returned by `ue4ssl_js_create`.
#[no_mangle]
pub unsafe extern "C" fn ue4ssl_js_load_scripts(engine: *mut JsEngine) -> c_int {
    with_engine_mut(engine, 0, JsEngine::load_scripts)
}

/// # Safety
///
/// `engine` must be null or a live pointer previously returned by `ue4ssl_js_create`.
#[no_mangle]
pub unsafe extern "C" fn ue4ssl_js_tick(engine: *mut JsEngine) -> c_int {
    with_engine_mut(engine, 0, JsEngine::tick)
}

/// # Safety
///
/// `engine` must be null or a live pointer previously returned by `ue4ssl_js_create`.
#[no_mangle]
pub unsafe extern "C" fn ue4ssl_js_is_initialized(engine: *const JsEngine) -> bool {
    with_engine(engine, false, JsEngine::is_initialized)
}

/// # Safety
///
/// `engine` must be null or a live pointer previously returned by `ue4ssl_js_create`.
/// `code` must point to `code_len` readable bytes when non-null; `filename` must be null
/// or point to a valid NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn ue4ssl_js_eval(
    engine: *mut JsEngine,
    code: *const c_char,
    code_len: usize,
    filename: *const c_char,
) -> bool {
    with_engine_mut(engine, false, |engine| {
        engine.eval(code, code_len, filename)
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_js_global_api_count() -> usize {
    GLOBAL_API.len()
}

#[no_mangle]
pub extern "C" fn ue4ssl_js_global_api_name(index: usize) -> *const c_char {
    GLOBAL_API
        .get(index)
        .map(|entry| entry.name.as_ptr().cast::<c_char>())
        .unwrap_or(ptr::null())
}

#[no_mangle]
pub extern "C" fn ue4ssl_js_global_api_arity(index: usize) -> c_int {
    GLOBAL_API.get(index).map(|entry| entry.arity).unwrap_or(-1)
}

#[no_mangle]
pub extern "C" fn ue4ssl_js_global_api_is_function(index: usize) -> bool {
    GLOBAL_API
        .get(index)
        .map(|entry| entry.arity >= 0)
        .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn api_manifest_tracks_compatibility_surface() {
        assert_eq!(GLOBAL_API.len(), 58);
        assert_eq!(GLOBAL_API[0].name, b"print\0");
        assert!(ue4ssl_js_global_api_is_function(0));
        assert!(GLOBAL_API
            .iter()
            .any(|entry| entry.name == b"__umgConstructWidget\0"));
        assert!(!ue4ssl_js_global_api_is_function(GLOBAL_API.len() - 1));
        assert_eq!(GLOBAL_API[GLOBAL_API.len() - 1].name, b"UE4SS\0");
    }
}
