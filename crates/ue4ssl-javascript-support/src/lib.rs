#![allow(clippy::missing_panics_doc)]

// This crate exists to host Cargo-managed QuickJS support archives.

#[no_mangle]
pub extern "C" fn ue4ssl_javascript_support_anchor() {}

#[used]
static FORCE_LINK_JAVASCRIPT_SUPPORT: extern "C" fn() = ue4ssl_javascript_support_anchor;
