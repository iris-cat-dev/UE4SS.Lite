use std::env;

use std::path::PathBuf;

use cc::Build;

use ue4ssl_build::common::{
    apply_common_defines, apply_common_msvc_flags, cc_archive_path, define,
    emit_core_import_library_link, emit_dylib_link, emit_rerun_for_tree, generate_abi_headers,
    object_searcher_include_dir, platform_include_dirs, require_paths_exist,
    require_relative_paths_exist, scanner_include_dir, ue4ssl_sdk_include_dirs,
    unreal_source_include_dirs, version_defines, whole_archive_flag,
    workspace_root_from_manifest_dir, BuildProfile,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();

    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();

    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-javascript currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));

    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);

    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("missing OUT_DIR"));

    let profile = BuildProfile::from_env();

    require_relative_paths_exist(
        "ue4ssl-javascript",
        &workspace_root,
        &[
            "scripts/ue4ssl-javascript/native/cpp",
            "scripts/ue4ssl-javascript/native/include",
            "scripts/ue4ssl-javascript-support/vendor/quickjs",
            "crates/ue4ssl-dll/native/UE4SSL/generated_src/version.cache",
        ],
    );

    require_paths_exist(
        "ue4ssl-javascript sources",
        javascript_sources(&workspace_root),
    );

    for tracked in [
        workspace_root.join("scripts/ue4ssl-javascript/native/cpp"),
        workspace_root.join("scripts/ue4ssl-javascript/native/include"),
        workspace_root.join("scripts/ue4ssl-javascript-support/vendor/quickjs"),
        workspace_root.join("crates/ue4ssl-dll/native/UE4SSL/generated_src"),
    ] {
        emit_rerun_for_tree(&tracked);
    }

    let generated = generate_abi_headers(&out_dir).expect("failed to generate ABI headers");
    let mut include_dirs = platform_include_dirs(&workspace_root);
    include_dirs.extend(ue4ssl_sdk_include_dirs(&workspace_root, &generated));
    include_dirs.extend([
        scanner_include_dir(&workspace_root),
        object_searcher_include_dir(&workspace_root),
        workspace_root.join("scripts/ue4ssl-javascript/native/include"),
        workspace_root.join("scripts/ue4ssl-javascript-support/vendor/quickjs"),
    ]);
    include_dirs.extend(unreal_source_include_dirs(&workspace_root));
    require_paths_exist("ue4ssl-javascript includes", &include_dirs);
    for include in &include_dirs {
        emit_rerun_for_tree(include);
    }

    compile_javascript_archive(&workspace_root, &include_dirs, profile);

    emit_dylib_link("winhttp");

    emit_dylib_link("winmm");

    emit_core_import_library_link(&workspace_root, profile).expect("failed to link UE4SSL core");

    let archive = cc_archive_path(&out_dir, "ue4ssl_javascript_mod_cpp");

    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&archive)
    );
}

fn compile_javascript_archive(
    workspace_root: &std::path::Path,

    include_dirs: &[PathBuf],

    profile: BuildProfile,
) {
    let mut build = Build::new();

    build.cargo_metadata(false);

    apply_common_msvc_flags(&mut build, profile, true, "/std:c++23preview");

    apply_common_defines(&mut build, profile);

    for include in include_dirs {
        build.include(include);
    }

    for (name, value) in version_defines(workspace_root, profile) {
        define(&mut build, &name, value.as_deref());
    }

    for source in javascript_sources(workspace_root) {
        build.file(source);
    }

    build.compile("ue4ssl_javascript_mod_cpp");
}

fn javascript_sources(workspace_root: &std::path::Path) -> Vec<PathBuf> {
    [
        "scripts/ue4ssl-javascript/native/cpp/dllmain.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSRustBridge.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSMod.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSHook.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSFetch.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSTimer.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSMemory.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSFileIO.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSAudio.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSDelegate.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp",
        "scripts/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp",
    ]
    .into_iter()
    .map(|relative| workspace_root.join(relative))
    .collect()
}
