pub mod core;
pub mod host;

#[inline(never)]
pub fn force_link_exports() {
    host::force_link_exports();
    core::force_link_exports();
}
