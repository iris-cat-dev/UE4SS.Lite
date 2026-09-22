use std::ffi::c_void;
#[cfg(windows)]
use std::ffi::{c_char, OsStr};
#[cfg(windows)]
use std::os::windows::ffi::OsStrExt;
use std::path::{Path, PathBuf};

use ue4ssl_abi::{CppModRuntimeStatus, RustModStartContext, SliceU16};

use super::wide;

const LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR: u32 = 0x0000_0100;
const LOAD_LIBRARY_SEARCH_DEFAULT_DIRS: u32 = 0x0000_1000;

const START_MOD_EXPORT: &[u8] = b"start_mod\0";
const UNINSTALL_MOD_EXPORT: &[u8] = b"uninstall_mod\0";
const RUST_START_MOD_EXPORT: &[u8] = b"ue4ssl_mod_start_v1\0";
const RUST_UNINSTALL_MOD_EXPORT: &[u8] = b"ue4ssl_mod_uninstall_v1\0";
const RUST_ON_UNREAL_INIT_EXPORT: &[u8] = b"ue4ssl_mod_on_unreal_init_v1\0";
const RUST_ON_UI_INIT_EXPORT: &[u8] = b"ue4ssl_mod_on_ui_init_v1\0";
const RUST_ON_PROGRAM_START_EXPORT: &[u8] = b"ue4ssl_mod_on_program_start_v1\0";
const RUST_ON_UPDATE_EXPORT: &[u8] = b"ue4ssl_mod_on_update_v1\0";
const RUST_ON_DLL_LOAD_EXPORT: &[u8] = b"ue4ssl_mod_on_dll_load_v1\0";

type RustStartModFn = unsafe extern "C" fn(*const RustModStartContext) -> *mut c_void;
type RustModFn = unsafe extern "C" fn(*mut c_void);
type RustDllLoadFn = unsafe extern "C" fn(*mut c_void, SliceU16);

#[repr(u32)]
#[derive(Clone, Copy, Eq, PartialEq)]
enum CppModFailureCode {
    None = 0,
    MissingDirectory = 1,
    LoadLibraryFailed = 2,
    MissingLifecycleExports = 3,
}

pub struct CppModHandle {
    mod_path: PathBuf,
    dlls_path_cookie: *mut c_void,
    main_dll_module: *mut c_void,
    start_mod_func: *mut c_void,
    uninstall_mod_func: *mut c_void,
    rust_start_mod_func: *mut c_void,
    rust_uninstall_mod_func: *mut c_void,
    rust_on_unreal_init_func: *mut c_void,
    rust_on_ui_init_func: *mut c_void,
    rust_on_program_start_func: *mut c_void,
    rust_on_update_func: *mut c_void,
    rust_on_dll_load_func: *mut c_void,
    mod_ptr: *mut c_void,
    rust_abi: bool,
    installable: bool,
    installed: bool,
    started: bool,
    updates_disabled: bool,
    failure_code: CppModFailureCode,
    last_error: u32,
}

impl Default for CppModHandle {
    fn default() -> Self {
        Self {
            mod_path: PathBuf::new(),
            dlls_path_cookie: std::ptr::null_mut(),
            main_dll_module: std::ptr::null_mut(),
            start_mod_func: std::ptr::null_mut(),
            uninstall_mod_func: std::ptr::null_mut(),
            rust_start_mod_func: std::ptr::null_mut(),
            rust_uninstall_mod_func: std::ptr::null_mut(),
            rust_on_unreal_init_func: std::ptr::null_mut(),
            rust_on_ui_init_func: std::ptr::null_mut(),
            rust_on_program_start_func: std::ptr::null_mut(),
            rust_on_update_func: std::ptr::null_mut(),
            rust_on_dll_load_func: std::ptr::null_mut(),
            mod_ptr: std::ptr::null_mut(),
            rust_abi: false,
            installable: true,
            installed: false,
            started: false,
            updates_disabled: false,
            failure_code: CppModFailureCode::None,
            last_error: 0,
        }
    }
}

impl CppModHandle {
    #[cfg(windows)]
    pub fn load(mod_path: SliceU16, dll_name: SliceU16) -> Box<Self> {
        let mod_path = wide::slice_to_path_buf(mod_path);
        let dll_name = wide::slice_to_os_string(dll_name);
        let dll_name = if dll_name.is_empty() {
            OsStr::new("main.dll").to_owned()
        } else {
            dll_name
        };

        let mut handle = Self {
            mod_path: mod_path.clone(),
            ..Self::default()
        };
        if !mod_path.exists() {
            handle.installable = false;
            handle.failure_code = CppModFailureCode::MissingDirectory;
            return Box::new(handle);
        }

        let dll_path = mod_path.join(dll_name);
        unsafe {
            handle.dlls_path_cookie = AddDllDirectory(path_to_wide_null(&mod_path).as_ptr());
            handle.main_dll_module = LoadLibraryExW(
                path_to_wide_null(&dll_path).as_ptr(),
                std::ptr::null_mut(),
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS,
            );
            if handle.main_dll_module.is_null() {
                handle.last_error = GetLastError();
                handle.failure_code = CppModFailureCode::LoadLibraryFailed;
                handle.installable = false;
                handle.remove_dll_directory();
                return Box::new(handle);
            }

            handle.start_mod_func =
                GetProcAddress(handle.main_dll_module, START_MOD_EXPORT.as_ptr().cast());
            handle.uninstall_mod_func =
                GetProcAddress(handle.main_dll_module, UNINSTALL_MOD_EXPORT.as_ptr().cast());
            handle.rust_start_mod_func = GetProcAddress(
                handle.main_dll_module,
                RUST_START_MOD_EXPORT.as_ptr().cast(),
            );
            handle.rust_uninstall_mod_func = GetProcAddress(
                handle.main_dll_module,
                RUST_UNINSTALL_MOD_EXPORT.as_ptr().cast(),
            );
            handle.rust_on_unreal_init_func = GetProcAddress(
                handle.main_dll_module,
                RUST_ON_UNREAL_INIT_EXPORT.as_ptr().cast(),
            );
            handle.rust_on_ui_init_func = GetProcAddress(
                handle.main_dll_module,
                RUST_ON_UI_INIT_EXPORT.as_ptr().cast(),
            );
            handle.rust_on_program_start_func = GetProcAddress(
                handle.main_dll_module,
                RUST_ON_PROGRAM_START_EXPORT.as_ptr().cast(),
            );
            handle.rust_on_update_func = GetProcAddress(
                handle.main_dll_module,
                RUST_ON_UPDATE_EXPORT.as_ptr().cast(),
            );
            handle.rust_on_dll_load_func = GetProcAddress(
                handle.main_dll_module,
                RUST_ON_DLL_LOAD_EXPORT.as_ptr().cast(),
            );

            let has_rust_lifecycle =
                !handle.rust_start_mod_func.is_null() || !handle.rust_uninstall_mod_func.is_null();
            let has_cpp_lifecycle =
                !handle.start_mod_func.is_null() || !handle.uninstall_mod_func.is_null();
            if has_rust_lifecycle {
                handle.rust_abi = true;
                if handle.rust_start_mod_func.is_null() || handle.rust_uninstall_mod_func.is_null()
                {
                    handle.failure_code = CppModFailureCode::MissingLifecycleExports;
                    handle.installable = false;
                    return Box::new(handle);
                }
            } else if !has_cpp_lifecycle
                || handle.start_mod_func.is_null()
                || handle.uninstall_mod_func.is_null()
            {
                handle.failure_code = CppModFailureCode::MissingLifecycleExports;
                handle.installable = false;
                return Box::new(handle);
            }
        }

        Box::new(handle)
    }

    #[cfg(all(test, not(windows)))]
    pub fn load(_mod_path: SliceU16, _dll_name: SliceU16) -> Box<Self> {
        panic!("Windows DLL loading is unavailable in non-Windows runtime tests");
    }

    pub fn status(&self) -> CppModRuntimeStatus {
        CppModRuntimeStatus {
            installable: u8::from(self.installable),
            installed: u8::from(self.installed),
            started: u8::from(self.started),
            updates_disabled: u8::from(self.updates_disabled),
            failure_code: self.failure_code as u32,
            last_error: self.last_error,
        }
    }

    pub fn owner(&self) -> usize {
        self.mod_ptr as usize
    }

    #[cfg(test)]
    pub(super) fn test_instance(
        context: *mut c_void,
        update: unsafe extern "C" fn(*mut c_void),
    ) -> Box<Self> {
        let mut handle = Self::default();
        handle.mod_ptr = context;
        handle.rust_abi = true;
        handle.installed = true;
        handle.started = true;
        handle.rust_on_update_func = update as *mut c_void;
        Box::new(handle)
    }

    pub fn set_installable(&mut self, value: bool) {
        self.installable = value;
    }

    pub fn set_installed(&mut self, value: bool) {
        self.installed = value;
    }

    pub fn set_updates_disabled(&mut self, value: bool) {
        self.updates_disabled = value;
    }

    pub fn start(&mut self) {
        if self.started || !self.installable {
            return;
        }

        self.mod_ptr = if self.rust_abi {
            self.call_rust_start()
        } else if !self.start_mod_func.is_null() {
            unsafe { ue4ssl_native_cppmod_call_start(self.start_mod_func) }
        } else {
            std::ptr::null_mut()
        };
        self.started = !self.mod_ptr.is_null();
    }

    pub fn uninstall(&mut self) {
        let mod_ptr = std::mem::replace(&mut self.mod_ptr, std::ptr::null_mut());
        self.started = false;
        self.installed = false;
        if !mod_ptr.is_null() {
            if self.rust_abi && !self.rust_uninstall_mod_func.is_null() {
                unsafe { call_rust_mod_fn(self.rust_uninstall_mod_func, mod_ptr) };
            } else if !self.uninstall_mod_func.is_null() {
                unsafe { ue4ssl_native_cppmod_call_uninstall(self.uninstall_mod_func, mod_ptr) };
            }
        }
    }

    pub fn fire_unreal_init(&mut self) {
        if !self.mod_ptr.is_null() {
            if self.rust_abi {
                unsafe { call_optional_rust_mod_fn(self.rust_on_unreal_init_func, self.mod_ptr) };
            } else {
                unsafe { ue4ssl_native_cppmod_call_on_unreal_init(self.mod_ptr) };
            }
        }
    }

    pub fn fire_ui_init(&mut self) {
        if !self.mod_ptr.is_null() {
            if self.rust_abi {
                unsafe { call_optional_rust_mod_fn(self.rust_on_ui_init_func, self.mod_ptr) };
            } else {
                unsafe { ue4ssl_native_cppmod_call_on_ui_init(self.mod_ptr) };
            }
        }
    }

    pub fn fire_program_start(&mut self) {
        if !self.mod_ptr.is_null() {
            if self.rust_abi {
                unsafe { call_optional_rust_mod_fn(self.rust_on_program_start_func, self.mod_ptr) };
            } else {
                unsafe { ue4ssl_native_cppmod_call_on_program_start(self.mod_ptr) };
            }
        }
    }

    pub fn fire_update(&mut self) {
        if !self.mod_ptr.is_null() {
            if self.rust_abi {
                unsafe { call_optional_rust_mod_fn(self.rust_on_update_func, self.mod_ptr) };
            } else {
                unsafe { ue4ssl_native_cppmod_call_on_update(self.mod_ptr) };
            }
        }
    }

    pub fn fire_dll_load(&mut self, dll_name: SliceU16) {
        if !self.mod_ptr.is_null() {
            if self.rust_abi {
                unsafe {
                    call_optional_rust_dll_load_fn(
                        self.rust_on_dll_load_func,
                        self.mod_ptr,
                        dll_name,
                    )
                };
            } else {
                unsafe {
                    ue4ssl_native_cppmod_call_on_dll_load(self.mod_ptr, dll_name.data, dll_name.len)
                };
            }
        }
    }

    fn call_rust_start(&self) -> *mut c_void {
        if self.rust_start_mod_func.is_null() {
            return std::ptr::null_mut();
        }

        let mod_path = path_to_wide(&self.mod_path);
        let context = RustModStartContext {
            mod_path: SliceU16 {
                data: mod_path.as_ptr(),
                len: mod_path.len(),
            },
        };
        unsafe {
            let start: RustStartModFn = std::mem::transmute(self.rust_start_mod_func);
            start(&context)
        }
    }

    #[cfg(windows)]
    unsafe fn free_library(&mut self) {
        if !self.main_dll_module.is_null() {
            ue4ssl_native_cppmod_free_library(self.main_dll_module);
            self.main_dll_module = std::ptr::null_mut();
        }
    }

    #[cfg(windows)]
    unsafe fn remove_dll_directory(&mut self) {
        if !self.dlls_path_cookie.is_null() {
            let _ = RemoveDllDirectory(self.dlls_path_cookie);
            self.dlls_path_cookie = std::ptr::null_mut();
        }
    }

    #[cfg(all(test, not(windows)))]
    unsafe fn free_library(&mut self) {
        assert!(
            self.main_dll_module.is_null(),
            "non-Windows tests cannot unload a Windows DLL"
        );
    }

    #[cfg(all(test, not(windows)))]
    unsafe fn remove_dll_directory(&mut self) {
        assert!(
            self.dlls_path_cookie.is_null(),
            "non-Windows tests cannot own a Windows DLL directory cookie"
        );
    }
}

impl Drop for CppModHandle {
    fn drop(&mut self) {
        unsafe {
            self.free_library();
            self.remove_dll_directory();
        }
    }
}

#[cfg(windows)]
fn path_to_wide_null(path: &Path) -> Vec<u16> {
    path.as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect()
}

fn path_to_wide(path: &Path) -> Vec<u16> {
    wide::os_str_to_utf16(path.as_os_str())
}

unsafe fn call_rust_mod_fn(fn_ptr: *mut c_void, mod_ptr: *mut c_void) {
    let callback: RustModFn = std::mem::transmute(fn_ptr);
    callback(mod_ptr);
}

unsafe fn call_optional_rust_mod_fn(fn_ptr: *mut c_void, mod_ptr: *mut c_void) {
    if !fn_ptr.is_null() {
        call_rust_mod_fn(fn_ptr, mod_ptr);
    }
}

unsafe fn call_optional_rust_dll_load_fn(
    fn_ptr: *mut c_void,
    mod_ptr: *mut c_void,
    dll_name: SliceU16,
) {
    if !fn_ptr.is_null() {
        let callback: RustDllLoadFn = std::mem::transmute(fn_ptr);
        callback(mod_ptr, dll_name);
    }
}

#[cfg(windows)]
#[link(name = "kernel32")]
extern "system" {
    fn AddDllDirectory(new_directory: *const u16) -> *mut c_void;
    fn RemoveDllDirectory(cookie: *mut c_void) -> i32;
    fn LoadLibraryExW(file_name: *const u16, file: *mut c_void, flags: u32) -> *mut c_void;
    fn GetProcAddress(module: *mut c_void, proc_name: *const c_char) -> *mut c_void;
    fn GetLastError() -> u32;
}

#[cfg(not(test))]
extern "C" {
    fn ue4ssl_native_cppmod_call_start(fn_ptr: *mut c_void) -> *mut c_void;
    fn ue4ssl_native_cppmod_call_uninstall(fn_ptr: *mut c_void, mod_ptr: *mut c_void);
    fn ue4ssl_native_cppmod_call_on_unreal_init(mod_ptr: *mut c_void);
    fn ue4ssl_native_cppmod_call_on_ui_init(mod_ptr: *mut c_void);
    fn ue4ssl_native_cppmod_call_on_program_start(mod_ptr: *mut c_void);
    fn ue4ssl_native_cppmod_call_on_update(mod_ptr: *mut c_void);
    fn ue4ssl_native_cppmod_call_on_dll_load(
        mod_ptr: *mut c_void,
        dll_name: *const u16,
        dll_name_len: usize,
    );
    fn ue4ssl_native_cppmod_free_library(module_handle: *mut c_void);
}

#[cfg(test)]
unsafe fn ue4ssl_native_cppmod_call_start(_fn_ptr: *mut c_void) -> *mut c_void {
    std::ptr::null_mut()
}

#[cfg(test)]
unsafe fn ue4ssl_native_cppmod_call_uninstall(_fn_ptr: *mut c_void, _mod_ptr: *mut c_void) {}

#[cfg(test)]
unsafe fn ue4ssl_native_cppmod_call_on_unreal_init(_mod_ptr: *mut c_void) {}

#[cfg(test)]
unsafe fn ue4ssl_native_cppmod_call_on_ui_init(_mod_ptr: *mut c_void) {}

#[cfg(test)]
unsafe fn ue4ssl_native_cppmod_call_on_program_start(_mod_ptr: *mut c_void) {}

#[cfg(test)]
unsafe fn ue4ssl_native_cppmod_call_on_update(_mod_ptr: *mut c_void) {}

#[cfg(test)]
unsafe fn ue4ssl_native_cppmod_call_on_dll_load(
    _mod_ptr: *mut c_void,
    _dll_name: *const u16,
    _dll_name_len: usize,
) {
}

#[cfg(test)]
unsafe fn ue4ssl_native_cppmod_free_library(_module_handle: *mut c_void) {}
