use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

type Module = *mut c_void;

#[link(name = "kernel32", kind = "raw-dylib")]
extern "system" {
    fn CreateThread(
        attributes: *const c_void,
        stack_size: usize,
        start: unsafe extern "system" fn(*mut c_void) -> u32,
        parameter: *mut c_void,
        flags: u32,
        thread_id: *mut u32,
    ) -> *mut c_void;
    fn CloseHandle(handle: *mut c_void) -> i32;
    fn GetModuleFileNameW(module: Module, buffer: *mut u16, size: u32) -> u32;
    fn GetModuleHandleExW(flags: u32, address: *const u16, module: *mut Module) -> i32;
    fn FreeLibraryAndExitThread(module: Module, code: u32) -> !;
}

extern "C" {
    fn ue4ssl_native_program_create(path: *const u16) -> *mut c_void;
    fn ue4ssl_native_program_run(program: *mut c_void);
    fn ue4ssl_native_program_destroy(program: *mut c_void);
    fn ue4ssl_runtime_request_shutdown();
}

unsafe fn run(module: Module) {
    let mut capacity = 1024;
    let path = loop {
        let mut path = vec![0u16; capacity];
        let len = GetModuleFileNameW(module, path.as_mut_ptr(), capacity as u32) as usize;
        if len == 0 {
            return;
        }
        if len < capacity {
            path.truncate(len + 1);
            break path;
        }
        if capacity >= 32768 {
            return;
        }
        capacity *= 2;
    };
    let program = ue4ssl_native_program_create(path.as_ptr());
    if program.is_null() {
        return;
    }
    ue4ssl_native_program_run(program);
    ue4ssl_native_program_destroy(program);
}

unsafe extern "system" fn worker(parameter: *mut c_void) -> u32 {
    let module = parameter;
    let _ = catch_unwind(AssertUnwindSafe(|| run(module)));
    // Do not return into this DLL after releasing the worker's module reference.
    FreeLibraryAndExitThread(module, 0);
}

// COFF linkage for the CRT entry, not an additional public DLL export.
core::arch::global_asm!(
    ".text",
    ".globl DllMain",
    "DllMain:",
    "jmp {entry}",
    entry = sym dll_main,
);

unsafe extern "system" fn dll_main(module: Module, reason: u32, _reserved: *mut c_void) -> i32 {
    match reason {
        1 => {
            // A dedicated reference keeps the worker code mapped until explicit shutdown.
            let mut retained = ptr::null_mut();
            if GetModuleHandleExW(4, dll_main as *const () as *const u16, &mut retained) == 0 {
                return 0;
            }
            let thread = CreateThread(ptr::null(), 0, worker, retained, 0, ptr::null_mut());
            if thread.is_null() {
                // Release only the additional reference; the loader still owns the attach reference.
                extern "system" {
                    fn FreeLibrary(module: Module) -> i32;
                }
                FreeLibrary(retained);
                return 0;
            }
            CloseHandle(thread);
            let _ = module;
        }
        0 => ue4ssl_runtime_request_shutdown(),
        _ => {}
    }
    1
}
