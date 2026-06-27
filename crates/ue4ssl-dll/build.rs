#[path = "../build_support/common.rs"]
mod common;

use std::env;
use std::path::PathBuf;

use common::{
    emit_dylib_link, emit_rerun_for_tree, support_archive_from_env, whole_archive_flag,
    workspace_root_from_manifest_dir,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-dll currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);

    for tracked in [workspace_root.join("crates").join("ue4ssl-dll").join("src")] {
        emit_rerun_for_tree(&tracked);
    }
    println!(
        "cargo:rerun-if-changed={}",
        workspace_root.join("Cargo.toml").display()
    );

    emit_dylib_link("dbghelp");
    emit_dylib_link("psapi");
    emit_dylib_link("d3d11");

    let support_archive = support_archive_from_env("ue4ssl_support", "archive");
    let native_support_archive = support_archive_from_env("ue4ssl_support", "native_archive");
    let unreal_archive = support_archive_from_env("ue4ssl_unreal_support", "archive");
    let object_searcher_archive = support_archive_from_env("ue4ssl_object_searcher", "archive");
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&support_archive)
    );
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&native_support_archive)
    );
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&unreal_archive)
    );
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&object_searcher_archive)
    );
}
