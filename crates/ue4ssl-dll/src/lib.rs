#![allow(non_snake_case)]

pub fn ensure_linked() {
    ue4ssl_support::ue4ssl_native_support_anchor();
    ue4ssl_unreal_support::ue4ssl_unreal_support_anchor();
    ue4ssl_support::ue4ssl_cpp_support_anchor();
    ue4ssl_runtime::force_link_exports();
    ue4ssl_hook::force_link_exports();
    patternsleuth_bind::force_link_exports();
}

#[used]
static FORCE_LINK_UE4SSL: fn() = ensure_linked;

#[cfg(windows)]
mod bootstrap;

#[cfg(windows)]
mod notifications;

/// Explicit resource shutdown, called outside the loader lock.
/// IAT callbacks pin the core module until process exit; this does not physically unload it.
#[no_mangle]
pub extern "C" fn ue4ssl_shutdown() -> u8 {
    ue4ssl_runtime::core::ue4ssl_runtime_shutdown()
}
