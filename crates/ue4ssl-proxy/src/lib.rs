#[cfg(not(all(target_os = "windows", target_env = "msvc", target_arch = "x86_64")))]
compile_error!("ue4ssl-proxy supports only x86_64-pc-windows-msvc");

use std::ffi::{c_void, OsStr, OsString};
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::os::windows::ffi::{OsStrExt, OsStringExt};
use std::path::{Path, PathBuf};
use std::ptr::{addr_of_mut, null_mut};

type Module = *mut c_void;

enum Export {
    Named(&'static [u8]),
    Ordinal(u16),
}

// Generated from the same PE export list as proxy.def and the unchanged MASM stubs.
include!(concat!(env!("OUT_DIR"), "/generated_proxy/exports.rs"));

static mut ORIGINAL_MODULE: Module = null_mut();

#[link(name = "kernel32")]
extern "system" {
    fn GetSystemDirectoryW(buffer: *mut u16, size: u32) -> u32;
    fn GetModuleFileNameW(module: Module, buffer: *mut u16, size: u32) -> u32;
    fn GetModuleHandleExW(flags: u32, address: *const u16, module: *mut Module) -> i32;
    fn LoadLibraryW(path: *const u16) -> Module;
    fn FreeLibrary(module: Module) -> i32;
    fn FreeLibraryAndExitThread(module: Module, exit_code: u32) -> !;
    fn GetProcAddress(module: Module, name: *const u8) -> *mut c_void;
    fn CreateThread(
        attributes: *mut c_void,
        stack_size: usize,
        start: unsafe extern "system" fn(*mut c_void) -> u32,
        parameter: *mut c_void,
        flags: u32,
        thread_id: *mut u32,
    ) -> *mut c_void;
    fn CloseHandle(handle: *mut c_void) -> i32;
    fn ExitProcess(exit_code: u32) -> !;
    fn OutputDebugStringA(message: *const u8);
    fn AreFileApisANSI() -> i32;
    fn MultiByteToWideChar(
        code_page: u32,
        flags: u32,
        input: *const u8,
        input_len: i32,
        output: *mut u16,
        output_len: i32,
    ) -> i32;
}

#[link(name = "ucrt")]
extern "C" {
    fn ___lc_codepage_func() -> u32;
}

#[link(name = "user32")]
extern "system" {
    fn MessageBoxW(window: *mut c_void, message: *const u16, caption: *const u16, kind: u32)
        -> i32;
}

unsafe fn load_original_dll() -> Option<Module> {
    let mut path = vec![0; 260];
    loop {
        let length = GetSystemDirectoryW(path.as_mut_ptr(), path.len() as u32) as usize;
        if length == 0 {
            return None;
        }
        if length < path.len() {
            path.truncate(length);
            break;
        }
        path.resize(length + 1, 0);
    }
    path.push(b'\\' as u16);
    path.extend_from_slice(ORIGINAL_DLL_NAME);
    let module = LoadLibraryW(path.as_ptr());
    if module.is_null() {
        return None;
    }

    // No stub may be observable with an unresolved destination. Initialization is
    // completed under the loader lock, before either attach returns or core starts.
    let procedures = addr_of_mut!(PROCEDURES).cast::<usize>();
    for (index, export) in EXPORTS.iter().enumerate() {
        let name = match export {
            Export::Named(name) => name.as_ptr(),
            Export::Ordinal(ordinal) => *ordinal as usize as *const u8,
        };
        let address = GetProcAddress(module, name);
        if address.is_null() {
            OutputDebugStringA(
                b"UE4SSL proxy: original DLL export is missing; rejecting attach\0".as_ptr(),
            );
            FreeLibrary(module);
            return None;
        }
        procedures.add(index).write(address as usize);
    }
    Some(module)
}

fn module_filename(module: Module) -> Option<PathBuf> {
    let mut buffer = vec![0; 1024];
    loop {
        let length = unsafe {
            GetModuleFileNameW(module, buffer.as_mut_ptr(), buffer.len() as u32) as usize
        };
        if length == 0 {
            return None;
        }
        if length < buffer.len() {
            return Some(PathBuf::from(OsString::from_wide(&buffer[..length])));
        }
        buffer.resize(buffer.len().checked_mul(2)?, 0);
    }
}

fn native_path(bytes: &[u8]) -> Option<PathBuf> {
    if bytes.is_empty() {
        return Some(PathBuf::new());
    }
    let length = i32::try_from(bytes.len()).ok()?;
    // Match MSVC filesystem: a UTF-8 CRT locale wins, otherwise use the native
    // file API code page. Do not strip a BOM or trim override whitespace.
    let code_page = if unsafe { ___lc_codepage_func() } == 65001 {
        65001
    } else if unsafe { AreFileApisANSI() } != 0 {
        0
    } else {
        1
    };
    let wide_length =
        unsafe { MultiByteToWideChar(code_page, 8, bytes.as_ptr(), length, null_mut(), 0) };
    if wide_length == 0 {
        return None;
    }
    let mut wide = vec![0; wide_length as usize];
    let converted = unsafe {
        MultiByteToWideChar(
            code_page,
            8,
            bytes.as_ptr(),
            length,
            wide.as_mut_ptr(),
            wide_length,
        )
    };
    if converted == 0 {
        return None;
    }
    Some(PathBuf::from(OsString::from_wide(
        &wide[..converted as usize],
    )))
}

fn is_separator(byte: u8) -> bool {
    byte == b'\\' || byte == b'/'
}

fn root_name_length(path: &[u8]) -> usize {
    if path.len() < 2 {
        return 0;
    }
    if path[0].is_ascii_alphabetic() && path[1] == b':' {
        return 2;
    }
    if !is_separator(path[0]) {
        return 0;
    }
    if path.len() >= 4
        && is_separator(path[3])
        && (path.len() == 4 || !is_separator(path[4]))
        && ((is_separator(path[1]) && matches!(path[2], b'?' | b'.'))
            || (path[1] == b'?' && path[2] == b'?'))
    {
        return 3;
    }
    if path.len() >= 3 && is_separator(path[1]) && !is_separator(path[2]) {
        return path[3..]
            .iter()
            .position(|byte| is_separator(*byte))
            .map_or(path.len(), |offset| offset + 3);
    }
    0
}

fn legacy_join(directory: &Path, child: &Path) -> PathBuf {
    // Match MSVC filesystem operator/ rather than Rust PathBuf::push: same-drive
    // C:relative appends; UNC root-name is the server, not server/share; verbatim
    // paths must retain dot components instead of Rust's lexical normalization.
    let base = directory.as_os_str().as_encoded_bytes();
    let suffix = child.as_os_str().as_encoded_bytes();
    let base_root = root_name_length(base);
    let suffix_root = root_name_length(suffix);
    let rooted = suffix.get(suffix_root).copied().is_some_and(is_separator);
    let absolute = if suffix_root == 2 && suffix[1] == b':' {
        rooted
    } else {
        suffix_root != 0
    };
    if absolute || (suffix_root != 0 && suffix[..suffix_root] != base[..base_root]) {
        return child.to_path_buf();
    }
    let base_end = if rooted { base_root } else { base.len() };
    // Every split is next to an ASCII path separator/colon or at the end, hence
    // at a valid boundary in OsStr's self-synchronizing encoded representation.
    let mut result =
        unsafe { OsStr::from_encoded_bytes_unchecked(&base[..base_end]) }.to_os_string();
    if !rooted
        && ((base_root == base.len() && base_root >= 3)
            || (base_root != base.len() && !is_separator(base[base.len() - 1])))
    {
        result.push("\\");
    }
    result.push(unsafe { OsStr::from_encoded_bytes_unchecked(&suffix[suffix_root..]) });
    PathBuf::from(result)
}

fn override_directory(current_path: &Path) -> Option<PathBuf> {
    let file = File::open(legacy_join(current_path, Path::new("override.txt"))).ok()?;
    let mut first_line = Vec::new();
    if BufReader::new(file)
        .read_until(b'\n', &mut first_line)
        .ok()?
        == 0
    {
        return None;
    }
    // Match getline on a Windows text-mode ifstream: CRLF translation and ^Z
    // EOF, but an empty first line is still a successful override of the directory.
    if let Some(end) = first_line.iter().position(|byte| *byte == 0x1a) {
        first_line.truncate(end);
        if first_line.is_empty() {
            return None;
        }
    } else if first_line.last() == Some(&b'\n') {
        first_line.pop();
        if first_line.last() == Some(&b'\r') {
            first_line.pop();
        }
    }
    let Some(override_path) = native_path(&first_line) else {
        unsafe {
            OutputDebugStringA(b"UE4SSL proxy: override.txt has invalid native encoding; trying default core paths\0".as_ptr());
        }
        return None;
    };
    Some(legacy_join(current_path, &override_path))
}

unsafe fn load_library(path: &Path) -> Module {
    let wide: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
    LoadLibraryW(wide.as_ptr())
}

unsafe fn load_core_dll(module: Module) -> Module {
    if let Some(filename) = module_filename(module) {
        if let Some(current_path) = filename.parent() {
            if let Some(override_path) = override_directory(current_path) {
                // Historical spelling is intentional: overrides load UE4SS.dll,
                // whereas both fallback paths load UE4SSL.dll.
                let core = load_library(&legacy_join(&override_path, Path::new("UE4SS.dll")));
                if !core.is_null() {
                    return core;
                }
            }
            let core = load_library(&legacy_join(current_path, Path::new("ue4ss\\UE4SSL.dll")));
            if !core.is_null() {
                return core;
            }
        }
    }
    load_library(Path::new("UE4SSL.dll"))
}

unsafe extern "system" fn load_core_thread(parameter: *mut c_void) -> u32 {
    if load_core_dll(parameter).is_null() {
        let message: Vec<u16> = "Failed to load UE4SSL.dll. Please see the docs on correct installation: https://docs.ue4ss.com/installation-guide"
            .encode_utf16().chain(Some(0)).collect();
        let caption: Vec<u16> = "UE4SS Error".encode_utf16().chain(Some(0)).collect();
        MessageBoxW(null_mut(), message.as_ptr(), caption.as_ptr(), 0x10);
        ExitProcess(0);
    }
    // Releasing the worker's reference and exiting must be atomic: returning
    // through Rust after FreeLibrary could execute code in an unloaded proxy.
    FreeLibraryAndExitThread(parameter, 0)
}

unsafe extern "system" fn dll_main(module: Module, reason: u32, reserved: *mut c_void) -> i32 {
    match reason {
        1 => {
            // The unchanged jump stubs require synchronous original-DLL loading.
            // This retains the original loader-lock limitation; it is not safe to
            // defer this step without also introducing a different forwarding ABI.
            let Some(original) = load_original_dll() else {
                OutputDebugStringA(
                    b"UE4SSL proxy: failed to initialize original DLL; rejecting attach\0".as_ptr(),
                );
                return 0;
            };
            ORIGINAL_MODULE = original;

            let mut retained = null_mut();
            // FROM_ADDRESS, without PIN: keep code alive only until worker exit.
            if GetModuleHandleExW(0x4, module.cast(), &mut retained) == 0 {
                FreeLibrary(original);
                ORIGINAL_MODULE = null_mut();
                return 0;
            }
            // Thread entry runs after loader notifications finish. Never wait in
            // DllMain. Core readiness is intentionally no longer an attach barrier.
            let thread = CreateThread(null_mut(), 0, load_core_thread, retained, 0, null_mut());
            if thread.is_null() {
                OutputDebugStringA(
                    b"UE4SSL proxy: failed to start core loader; rejecting attach\0".as_ptr(),
                );
                // The caller's attach reference still exists, so this decrement
                // cannot unload the proxy while DllMain is executing.
                FreeLibrary(retained);
                FreeLibrary(original);
                ORIGINAL_MODULE = null_mut();
                return 0;
            }
            CloseHandle(thread);
        }
        0 if reserved.is_null() => {
            // Preserve the original reference balance for explicit unloads only.
            // Like synchronous loading, this remains a loader-lock restriction.
            // During process termination Windows tears modules down itself.
            let original = ORIGINAL_MODULE;
            ORIGINAL_MODULE = null_mut();
            if !original.is_null() {
                FreeLibrary(original);
            }
        }
        _ => {}
    }
    1
}
