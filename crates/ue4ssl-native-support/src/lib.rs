#[no_mangle]
pub extern "C" fn ue4ssl_native_support_anchor() {}

#[used]
static FORCE_LINK_NATIVE_SUPPORT: extern "C" fn() = ue4ssl_native_support_anchor;

#[repr(C)]
#[derive(Default)]
pub struct NativeOwnedString {
    pub data: *mut u16,
    pub len: usize,
}

mod file_io {
    use super::NativeOwnedString;
    use std::ffi::OsString;
    use std::fs::{self, OpenOptions};
    use std::io::Write;
    use std::path::PathBuf;
    use std::ptr;

    #[cfg(windows)]
    use std::os::windows::ffi::OsStringExt;

    pub unsafe fn path_from_utf16_z(path: *const u16) -> Option<PathBuf> {
        if path.is_null() {
            return None;
        }

        let mut len = 0;
        while ptr::read(path.add(len)) != 0 {
            len += 1;
        }

        #[cfg(windows)]
        {
            Some(OsString::from_wide(std::slice::from_raw_parts(path, len)).into())
        }

        #[cfg(not(windows))]
        {
            let _ = len;
            None
        }
    }

    pub fn owned_string_from_utf16(mut data: Vec<u16>) -> NativeOwnedString {
        let output = NativeOwnedString {
            data: data.as_mut_ptr(),
            len: data.len(),
        };
        std::mem::forget(data);
        output
    }

    pub unsafe fn free_owned_string(string: NativeOwnedString) {
        if !string.data.is_null() {
            drop(Vec::from_raw_parts(string.data, string.len, string.len));
        }
    }

    pub unsafe fn read_to_string(path: *const u16) -> NativeOwnedString {
        let Some(path) = path_from_utf16_z(path) else {
            return NativeOwnedString::default();
        };
        let Ok(bytes) = fs::read(path) else {
            return NativeOwnedString::default();
        };

        let decoded = decode_text(&bytes);
        owned_string_from_utf16(decoded.encode_utf16().collect())
    }

    pub unsafe fn prepare_append(path: *const u16, truncate_existing: u8) -> bool {
        let Some(path) = path_from_utf16_z(path) else {
            return false;
        };

        if let Some(parent) = path.parent() {
            if !parent.as_os_str().is_empty() && fs::create_dir_all(parent).is_err() {
                return false;
            }
        }

        OpenOptions::new()
            .create(true)
            .write(true)
            .append(truncate_existing == 0)
            .truncate(truncate_existing != 0)
            .open(path)
            .is_ok()
    }

    pub unsafe fn append_utf16(path: *const u16, data: *const u16, len: usize) -> bool {
        let Some(path) = path_from_utf16_z(path) else {
            return false;
        };
        let data = if data.is_null() {
            &[]
        } else {
            std::slice::from_raw_parts(data, len)
        };
        let text = String::from_utf16_lossy(data);

        OpenOptions::new()
            .create(true)
            .append(true)
            .open(path)
            .and_then(|mut file| file.write_all(text.as_bytes()))
            .is_ok()
    }

    fn decode_text(bytes: &[u8]) -> String {
        if bytes.starts_with(&[0xEF, 0xBB, 0xBF]) {
            return String::from_utf8_lossy(&bytes[3..]).into_owned();
        }

        if bytes.starts_with(&[0xFF, 0xFE]) {
            let units = bytes[2..]
                .chunks_exact(2)
                .map(|chunk| u16::from_le_bytes([chunk[0], chunk[1]]))
                .collect::<Vec<_>>();
            return String::from_utf16_lossy(&units);
        }

        if bytes.starts_with(&[0xFE, 0xFF]) {
            let units = bytes[2..]
                .chunks_exact(2)
                .map(|chunk| u16::from_be_bytes([chunk[0], chunk[1]]))
                .collect::<Vec<_>>();
            return String::from_utf16_lossy(&units);
        }

        String::from_utf8_lossy(bytes).into_owned()
    }
}

#[cfg(windows)]
mod input {
    use std::ffi::c_void;
    use std::ptr;

    const MAX_CLASS_NAME: usize = 260;

    #[link(name = "user32")]
    extern "system" {
        fn GetAsyncKeyState(v_key: i32) -> i16;
        fn GetForegroundWindow() -> *mut c_void;
        fn GetClassNameW(hwnd: *mut c_void, class_name: *mut u16, max_count: i32) -> i32;
    }

    pub fn is_key_down(v_key: i32) -> bool {
        unsafe { GetAsyncKeyState(v_key) != 0 }
    }

    pub fn foreground_class_matches(expected_class_name: *const u16) -> bool {
        if expected_class_name.is_null() {
            return false;
        }

        let hwnd = unsafe { GetForegroundWindow() };
        if hwnd.is_null() {
            return false;
        }

        let mut class_name = [0_u16; MAX_CLASS_NAME];
        let len = unsafe { GetClassNameW(hwnd, class_name.as_mut_ptr(), MAX_CLASS_NAME as i32) };
        if len <= 0 {
            return false;
        }

        let len = len as usize;
        let mut expected_len = 0;
        while expected_len < MAX_CLASS_NAME {
            let value = unsafe { ptr::read(expected_class_name.add(expected_len)) };
            if value == 0 {
                break;
            }
            expected_len += 1;
        }

        expected_len == len
            && class_name[..len]
                == unsafe { std::slice::from_raw_parts(expected_class_name, expected_len) }[..]
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_file_read_to_string(path: *const u16) -> NativeOwnedString {
    unsafe { file_io::read_to_string(path) }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_file_free_string(string: NativeOwnedString) {
    unsafe {
        file_io::free_owned_string(string);
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_file_prepare_append(path: *const u16, truncate_existing: u8) -> u8 {
    unsafe { u8::from(file_io::prepare_append(path, truncate_existing)) }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_file_append_utf16(
    path: *const u16,
    data: *const u16,
    len: usize,
) -> u8 {
    unsafe { u8::from(file_io::append_utf16(path, data, len)) }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_input_is_key_down(v_key: i32) -> u8 {
    #[cfg(windows)]
    {
        u8::from(input::is_key_down(v_key))
    }

    #[cfg(not(windows))]
    {
        let _ = v_key;
        0
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_input_foreground_class_matches(class_name: *const u16) -> u8 {
    #[cfg(windows)]
    {
        u8::from(input::foreground_class_matches(class_name))
    }

    #[cfg(not(windows))]
    {
        let _ = class_name;
        0
    }
}
