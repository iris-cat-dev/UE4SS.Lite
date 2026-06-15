#[no_mangle]
pub extern "C" fn ue4ssl_native_support_anchor() {}

#[used]
static FORCE_LINK_NATIVE_SUPPORT: extern "C" fn() = ue4ssl_native_support_anchor;

#[no_mangle]
pub extern "C" fn ue4ssl_cpp_support_anchor() {}

#[used]
static FORCE_LINK_UE4SSL_CPP_SUPPORT: extern "C" fn() = ue4ssl_cpp_support_anchor;

#[repr(C)]
#[derive(Default)]
pub struct NativeOwnedString {
    pub data: *mut u16,
    pub len: usize,
}

mod file_io {
    use super::NativeOwnedString;
    use std::fs::{self, OpenOptions};
    use std::io::Write;
    use std::path::PathBuf;
    use std::ptr;

    #[cfg(windows)]
    use std::ffi::OsString;
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
    use std::collections::HashMap;
    use std::ffi::c_void;
    use std::ptr;

    const MAX_CLASS_NAME: usize = 260;
    const MOD_KEY_START_OF_ENUM: u8 = 0x0;
    const SHIFT: u8 = 0x10;
    const CONTROL: u8 = 0x11;
    const ALT: u8 = 0x12;

    type EventCallback = unsafe extern "C" fn(data: *mut c_void);

    #[derive(Clone, Copy)]
    struct Callback {
        call: EventCallback,
        data: *mut c_void,
    }

    struct KeyData {
        required_modifier_keys: Vec<u8>,
        callbacks: Vec<Callback>,
        requires_modifier_keys: bool,
        is_down: bool,
    }

    #[derive(Default)]
    struct KeySet {
        key_data: HashMap<u8, Vec<KeyData>>,
    }

    pub struct Handler {
        active_window_classes: Vec<Vec<u16>>,
        key_sets: Vec<KeySet>,
        modifier_keys_down: HashMap<u8, bool>,
        any_keys_are_down: bool,
        allow_input: bool,
    }

    impl Handler {
        pub fn new() -> Self {
            let mut modifier_keys_down = HashMap::new();
            modifier_keys_down.insert(SHIFT, false);
            modifier_keys_down.insert(CONTROL, false);
            modifier_keys_down.insert(ALT, false);

            Self {
                active_window_classes: Vec::new(),
                key_sets: Vec::new(),
                modifier_keys_down,
                any_keys_are_down: false,
                allow_input: true,
            }
        }

        pub unsafe fn add_window_class(&mut self, class_name: *const u16) {
            let Some(class_name) = read_utf16_z(class_name) else {
                return;
            };
            self.active_window_classes.push(class_name);
        }

        pub unsafe fn register_keydown_event(
            &mut self,
            key: u8,
            modifier_keys: *const u8,
            modifier_key_count: usize,
            callback: Option<EventCallback>,
            callback_data: *mut c_void,
        ) {
            let Some(callback) = callback else {
                return;
            };

            let required_modifier_keys = if modifier_keys.is_null() || modifier_key_count == 0 {
                Vec::new()
            } else {
                std::slice::from_raw_parts(modifier_keys, modifier_key_count)
                    .iter()
                    .copied()
                    .filter(|key| *key != MOD_KEY_START_OF_ENUM)
                    .collect()
            };

            let key_set_index = self
                .key_sets
                .iter()
                .position(|key_set| key_set.key_data.contains_key(&key))
                .unwrap_or_else(|| {
                    self.key_sets.push(KeySet::default());
                    self.key_sets.len() - 1
                });

            self.key_sets[key_set_index]
                .key_data
                .entry(key)
                .or_default()
                .push(KeyData {
                    requires_modifier_keys: !required_modifier_keys.is_empty(),
                    required_modifier_keys,
                    callbacks: vec![Callback {
                        call: callback,
                        data: callback_data,
                    }],
                    is_down: false,
                });
        }

        pub fn process_event(&mut self) {
            if !self.is_program_focused() {
                return;
            }

            let mut callbacks_to_call = Vec::new();
            let mut skip_this_frame = !self.allow_input;
            let mut is_any_modifier_keys_down = false;
            let mut any_keys_are_down = false;

            if self.any_keys_are_down {
                skip_this_frame = true;
            }

            for (modifier_key, key_is_down) in &mut self.modifier_keys_down {
                if is_key_down(*modifier_key as i32) {
                    is_any_modifier_keys_down = true;
                    *key_is_down = true;
                } else {
                    *key_is_down = false;
                }
            }

            for key_set_data in &mut self.key_sets {
                for (key, key_data_array) in &mut key_set_data.key_data {
                    for key_data in key_data_array {
                        if is_key_down(*key as i32) && !key_data.is_down {
                            any_keys_are_down = true;
                            let mut should_propagate = true;

                            if key_data.requires_modifier_keys
                                && !are_modifier_keys_down(
                                    &self.modifier_keys_down,
                                    &key_data.required_modifier_keys,
                                )
                            {
                                should_propagate = false;
                            }

                            if !key_data.requires_modifier_keys && is_any_modifier_keys_down {
                                should_propagate = false;
                            }

                            if should_propagate {
                                key_data.is_down = true;
                                callbacks_to_call.extend(key_data.callbacks.iter().copied());
                            }
                        } else if !is_key_down(*key as i32) && key_data.is_down {
                            key_data.is_down = false;
                        }
                    }
                }
            }

            self.any_keys_are_down = any_keys_are_down;

            for callback in callbacks_to_call {
                if skip_this_frame {
                    return;
                }

                unsafe {
                    (callback.call)(callback.data);
                }
            }
        }

        pub fn allow_input(&self) -> bool {
            self.allow_input
        }

        pub fn set_allow_input(&mut self, allow_input: bool) {
            self.allow_input = allow_input;
        }

        fn is_program_focused(&self) -> bool {
            self.active_window_classes
                .iter()
                .any(|class_name| foreground_class_matches(class_name.as_ptr()))
        }
    }

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

    unsafe fn read_utf16_z(value: *const u16) -> Option<Vec<u16>> {
        if value.is_null() {
            return None;
        }

        let mut len = 0;
        while ptr::read(value.add(len)) != 0 {
            len += 1;
        }

        let mut output = std::slice::from_raw_parts(value, len).to_vec();
        output.push(0);
        Some(output)
    }

    fn is_modifier_key_required(modifier_key: u8, modifier_keys: &[u8]) -> bool {
        modifier_keys
            .iter()
            .any(|required_modifier_key| *required_modifier_key == modifier_key)
    }

    fn are_modifier_keys_down(
        modifier_keys_down: &HashMap<u8, bool>,
        required_modifier_keys: &[u8],
    ) -> bool {
        let mut are_required_modifier_keys_down = true;

        for (modifier_key, modifier_key_is_down) in modifier_keys_down {
            for required_modifier_key in required_modifier_keys {
                if modifier_key == required_modifier_key && !modifier_key_is_down {
                    are_required_modifier_keys_down = false;
                }

                if modifier_key != required_modifier_key
                    && *modifier_key_is_down
                    && !is_modifier_key_required(*modifier_key, required_modifier_keys)
                {
                    return false;
                }
            }
        }

        are_required_modifier_keys_down
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

#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_new() -> *mut std::ffi::c_void {
    #[cfg(windows)]
    {
        Box::into_raw(Box::new(input::Handler::new())).cast()
    }

    #[cfg(not(windows))]
    {
        std::ptr::null_mut()
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_destroy(handler: *mut std::ffi::c_void) {
    #[cfg(windows)]
    {
        if !handler.is_null() {
            unsafe {
                drop(Box::from_raw(handler.cast::<input::Handler>()));
            }
        }
    }

    #[cfg(not(windows))]
    {
        let _ = handler;
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_add_window_class(
    handler: *mut std::ffi::c_void,
    class_name: *const u16,
) {
    #[cfg(windows)]
    {
        if !handler.is_null() {
            unsafe {
                (*handler.cast::<input::Handler>()).add_window_class(class_name);
            }
        }
    }

    #[cfg(not(windows))]
    {
        let _ = (handler, class_name);
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_register_keydown_event(
    handler: *mut std::ffi::c_void,
    key: u8,
    modifier_keys: *const u8,
    modifier_key_count: usize,
    callback: Option<unsafe extern "C" fn(data: *mut std::ffi::c_void)>,
    callback_data: *mut std::ffi::c_void,
) {
    #[cfg(windows)]
    {
        if !handler.is_null() {
            unsafe {
                (*handler.cast::<input::Handler>()).register_keydown_event(
                    key,
                    modifier_keys,
                    modifier_key_count,
                    callback,
                    callback_data,
                );
            }
        }
    }

    #[cfg(not(windows))]
    {
        let _ = (
            handler,
            key,
            modifier_keys,
            modifier_key_count,
            callback,
            callback_data,
        );
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_process_event(handler: *mut std::ffi::c_void) {
    #[cfg(windows)]
    {
        if !handler.is_null() {
            unsafe {
                (*handler.cast::<input::Handler>()).process_event();
            }
        }
    }

    #[cfg(not(windows))]
    {
        let _ = handler;
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_get_allow_input(
    handler: *mut std::ffi::c_void,
) -> u8 {
    #[cfg(windows)]
    {
        if handler.is_null() {
            return 0;
        }

        unsafe { u8::from((*handler.cast::<input::Handler>()).allow_input()) }
    }

    #[cfg(not(windows))]
    {
        let _ = handler;
        0
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_set_allow_input(
    handler: *mut std::ffi::c_void,
    allow_input: u8,
) {
    #[cfg(windows)]
    {
        if !handler.is_null() {
            unsafe {
                (*handler.cast::<input::Handler>()).set_allow_input(allow_input != 0);
            }
        }
    }

    #[cfg(not(windows))]
    {
        let _ = (handler, allow_input);
    }
}
