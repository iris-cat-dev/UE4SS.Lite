#![allow(non_snake_case)]

pub fn ensure_linked() {
    ue4ssl_native_support::ue4ssl_native_support_anchor();
    ue4ssl_unreal_support::ue4ssl_unreal_support_anchor();
    ue4ssl_cpp_support::ue4ssl_cpp_support_anchor();
    ue4ssl_host::force_link_exports();
    ue4ssl_core::force_link_exports();
    ue4ssl_hook::force_link_exports();
    patternsleuth_bind::force_link_exports();
}

#[used]
static FORCE_LINK_UE4SSL: fn() = ensure_linked;
