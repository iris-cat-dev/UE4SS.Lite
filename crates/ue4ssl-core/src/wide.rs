use std::ffi::{OsStr, OsString};
use std::os::windows::ffi::{OsStrExt, OsStringExt};
use std::path::{Path, PathBuf};
use std::ptr;
use std::slice;

use super::{OwnedString, SliceU16};

pub fn slice_to_os_string(value: SliceU16) -> OsString {
    if value.data.is_null() || value.len == 0 {
        return OsString::new();
    }

    let raw = unsafe { slice::from_raw_parts(value.data, value.len) };
    OsString::from_wide(raw)
}

pub fn slice_to_string_lossy(value: SliceU16) -> String {
    if value.data.is_null() || value.len == 0 {
        return String::new();
    }

    let raw = unsafe { slice::from_raw_parts(value.data, value.len) };
    String::from_utf16_lossy(raw)
}

pub fn slice_to_path_buf(value: SliceU16) -> PathBuf {
    PathBuf::from(slice_to_os_string(value))
}

pub fn os_str_to_owned_string(value: &OsStr) -> OwnedString {
    let encoded: Vec<u16> = value.encode_wide().collect();
    vec_to_owned_string(encoded)
}

pub fn str_to_owned_string(value: &str) -> OwnedString {
    let encoded: Vec<u16> = value.encode_utf16().collect();
    vec_to_owned_string(encoded)
}

pub fn path_to_owned_string(value: &Path) -> OwnedString {
    os_str_to_owned_string(value.as_os_str())
}

fn vec_to_owned_string(value: Vec<u16>) -> OwnedString {
    if value.is_empty() {
        return OwnedString::default();
    }

    let mut boxed = value.into_boxed_slice();
    let data = boxed.as_mut_ptr();
    let len = boxed.len();
    std::mem::forget(boxed);

    OwnedString { data, len }
}

pub unsafe fn free_owned_string_in_place(value: &mut OwnedString) {
    if !value.data.is_null() {
        let raw = ptr::slice_from_raw_parts_mut(value.data, value.len);
        drop(Box::from_raw(raw));
    }

    value.data = ptr::null_mut();
    value.len = 0;
}
