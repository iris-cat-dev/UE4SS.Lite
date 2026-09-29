use std::env;
use std::path::PathBuf;

use cc::Build;
use ue4ssl_build::common::{
    cc_archive_path, emit_rerun_for_tree, emit_static_archive_metadata, require_paths_exist,
    scanner_include_dir, workspace_root_from_manifest_dir, BuildProfile,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    // The Rust scanning FFI is also usable on non-Windows hosts; only its
    // legacy Win32 C++ adapter needs the MSVC toolchain.
    if target_os != "windows" {
        return;
    }
    if target_env != "msvc" {
        panic!("patternsleuth_bind native scanner currently requires windows-msvc");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("missing OUT_DIR"));
    let profile = BuildProfile::from_env();
    let scanner_root = workspace_root.join("crates/patternsleuth-bind/native/SinglePassSigScanner");
    let source = scanner_root.join("src/SinglePassScannerShim.cpp");
    let include = scanner_include_dir(&workspace_root);
    require_paths_exist(
        "patternsleuth_bind native scanner inputs",
        [&source, &include],
    );
    emit_rerun_for_tree(&scanner_root);

    println!("cargo:rustc-link-lib=kernel32");
    println!("cargo:rustc-link-lib=psapi");

    let mut build = Build::new();
    build.cargo_metadata(false);
    build.cpp(true);
    build.static_crt(false);
    build.debug(profile.is_debug());
    build.flag("-MD");
    build.include(include);
    build.file(source);

    for define in [
        "_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR=1",
        "RC_SINGLE_PASS_SIG_SCANNER_EXPORTS",
        "RC_SINGLE_PASS_SIG_SCANNER_BUILD_STATIC",
        "_ITERATOR_DEBUG_LEVEL=0",
    ] {
        if let Some((name, value)) = define.split_once('=') {
            build.define(name, Some(value));
        } else {
            build.define(define, None);
        }
    }

    build.flag("/std:c++23preview");
    build.flag("/EHsc");
    build.flag("/utf-8");
    build.flag("/MP");
    build.flag("/wd4005");
    build.flag("/wd4068");
    build.flag("/wd4251");
    build.flag("/Zc:inline");
    build.flag("/Zc:strictStrings");
    build.flag("/Zc:preprocessor");

    build.compile("ue4ssl_scanner_cpp");

    let archive = cc_archive_path(&out_dir, "ue4ssl_scanner_cpp");
    emit_static_archive_metadata("archive", &archive);
}
