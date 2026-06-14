#![allow(non_snake_case)]

#[no_mangle]
pub extern "C" fn ue4ssl_cpp_support_anchor() {}

#[used]
static FORCE_LINK_UE4SSL_CPP_SUPPORT: extern "C" fn() = ue4ssl_cpp_support_anchor;
