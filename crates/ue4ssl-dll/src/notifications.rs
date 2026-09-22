use std::ffi::{c_char, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{
    atomic::{AtomicBool, AtomicU64, Ordering},
    Mutex,
};
use ue4ssl_abi::SliceU16;
use ue4ssl_hook::{
    ue4ss_iat_hook_create, ue4ss_iat_hook_destroy, ue4ss_iat_hook_hook, ue4ss_iat_hook_unhook,
};

static ENABLED: AtomicBool = AtomicBool::new(false);
static INSTALLED: AtomicBool = AtomicBool::new(false);
static STOPPED: AtomicBool = AtomicBool::new(false);
static HOOKS: Mutex<Vec<usize>> = Mutex::new(Vec::new());
// The original functions remain callable after unhook; they belong to kernel32.
static ORIGINALS: [AtomicU64; 4] = [const { AtomicU64::new(0) }; 4];

#[link(name = "kernel32", kind = "raw-dylib")]
extern "system" {
    fn GetLastError() -> u32;
    fn SetLastError(error: u32);
    fn MultiByteToWideChar(
        codepage: u32,
        flags: u32,
        text: *const c_char,
        len: i32,
        output: *mut u16,
        capacity: i32,
    ) -> i32;
    fn GetModuleHandleExW(flags: u32, address: *const u16, module: *mut *mut c_void) -> i32;
}
extern "C" {
    fn ue4ssl_runtime_dll_load(name: SliceU16);
}

unsafe fn notify_wide(name: *const u16) {
    if name.is_null() || !ENABLED.load(Ordering::Acquire) {
        return;
    }
    let saved = GetLastError();
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let mut len = 0;
        while *name.add(len) != 0 {
            len += 1;
        }
        ue4ssl_runtime_dll_load(SliceU16 { data: name, len });
    }));
    SetLastError(saved);
}
unsafe fn notify_ansi(name: *const c_char) {
    if name.is_null() || !ENABLED.load(Ordering::Acquire) {
        return;
    }
    let saved = GetLastError();
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let len = MultiByteToWideChar(0, 0, name, -1, std::ptr::null_mut(), 0);
        if len > 0 {
            let mut wide = vec![0u16; len as usize];
            if MultiByteToWideChar(0, 0, name, -1, wide.as_mut_ptr(), len) > 0 {
                notify_wide(wide.as_ptr());
            }
        }
    }));
    SetLastError(saved);
}
unsafe extern "system" fn load_a(name: *const c_char) -> *mut c_void {
    let original: unsafe extern "system" fn(*const c_char) -> *mut c_void =
        std::mem::transmute(ORIGINALS[0].load(Ordering::Acquire));
    let result = original(name);
    notify_ansi(name);
    result
}
unsafe extern "system" fn load_ex_a(
    name: *const c_char,
    file: *mut c_void,
    flags: u32,
) -> *mut c_void {
    let original: unsafe extern "system" fn(*const c_char, *mut c_void, u32) -> *mut c_void =
        std::mem::transmute(ORIGINALS[1].load(Ordering::Acquire));
    let result = original(name, file, flags);
    notify_ansi(name);
    result
}
unsafe extern "system" fn load_w(name: *const u16) -> *mut c_void {
    let original: unsafe extern "system" fn(*const u16) -> *mut c_void =
        std::mem::transmute(ORIGINALS[2].load(Ordering::Acquire));
    let result = original(name);
    notify_wide(name);
    result
}
unsafe extern "system" fn load_ex_w(
    name: *const u16,
    file: *mut c_void,
    flags: u32,
) -> *mut c_void {
    let original: unsafe extern "system" fn(*const u16, *mut c_void, u32) -> *mut c_void =
        std::mem::transmute(ORIGINALS[3].load(Ordering::Acquire));
    let result = original(name, file, flags);
    notify_wide(name);
    result
}
#[no_mangle]
pub extern "C" fn ue4ssl_install_dll_notifications() -> u8 {
    catch_unwind(|| {
        if INSTALLED.load(Ordering::Acquire) {
            return u8::from(ENABLED.load(Ordering::Acquire));
        }
        let mut pinned = std::ptr::null_mut();
        // An external thread may cache an IAT target before unhook and enter it
        // later. Pin the trampoline module; shutdown still releases Mod/VM resources.
        if unsafe { GetModuleHandleExW(5, load_a as *const () as *const u16, &mut pinned) } == 0 {
            return 0;
        }
        let mut hooks = Vec::new();
        if INSTALLED.swap(true, Ordering::AcqRel) {
            return u8::from(ENABLED.load(Ordering::Acquire));
        }
        let entries: [(&[u8], u64); 4] = [
            (b"LoadLibraryA\0", load_a as *const () as u64),
            (b"LoadLibraryExA\0", load_ex_a as *const () as u64),
            (b"LoadLibraryW\0", load_w as *const () as u64),
            (b"LoadLibraryExW\0", load_ex_w as *const () as u64),
        ];
        for (index, (name, callback)) in entries.iter().enumerate() {
            let handle = ue4ss_iat_hook_create(
                b"kernel32.dll\0".as_ptr().cast(),
                name.as_ptr().cast(),
                *callback,
                ORIGINALS[index].as_ptr(),
                std::ptr::null(),
            );
            if handle.is_null() {
                continue;
            }
            if ue4ss_iat_hook_hook(handle) {
                hooks.push(handle as usize);
            } else {
                ue4ss_iat_hook_destroy(handle);
            }
        }
        let mut published = HOOKS.lock().unwrap_or_else(|e| e.into_inner());
        if STOPPED.load(Ordering::Acquire) {
            drop(published);
            release_hooks(hooks);
            return 0;
        }
        let active = !hooks.is_empty();
        *published = hooks;
        ENABLED.store(active, Ordering::Release);
        u8::from(active)
    })
    .unwrap_or(0)
}
#[no_mangle]
pub extern "C" fn ue4ssl_stop_dll_notifications() {
    let _ = catch_unwind(|| {
        let hooks = {
            let mut published = HOOKS.lock().unwrap_or_else(|e| e.into_inner());
            STOPPED.store(true, Ordering::Release);
            ENABLED.store(false, Ordering::Release);
            std::mem::take(&mut *published)
        };
        release_hooks(hooks);
    });
}

fn release_hooks(hooks: Vec<usize>) {
    for handle in hooks {
        let ptr = handle as *mut ue4ssl_hook::Ue4ssHookIatHookHandle;
        if ue4ss_iat_hook_unhook(ptr) {
            ue4ss_iat_hook_destroy(ptr);
        }
        // Failed restoration retains both the backend and its source-module reference.
    }
}
