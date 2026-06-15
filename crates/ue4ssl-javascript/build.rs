#[path = "../build_support/common.rs"]
mod common;

use std::env;

use std::path::PathBuf;

use cc::Build;

use common::{
    apply_common_defines, apply_common_msvc_flags, cc_archive_path, collect_sources, define,
    emit_dylib_link, emit_rerun_for_tree, generate_abi_headers, require_paths_exist,
    require_relative_paths_exist, target_dir, version_defines, whole_archive_flag,
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
            "crates/ue4ssl-javascript/native/cpp",
            "crates/ue4ssl-javascript/native/include",
            "crates/ue4ssl-javascript-support/vendor/quickjs",
            "crates/ue4ssl-cpp-support/vendor/UE4SSL/include",
            "crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_include",
            "crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_src/version.cache",
            "crates/ue4ssl-unreal-support/vendor/Unreal/src",
            "crates/ue4ssl-unreal-support/vendor/Unreal/include",
            "crates/ue4ssl-unreal-support/vendor/Unreal/generated_include",
            "crates/ue4ssl-native-support/vendor/Input/include",
            "crates/ue4ssl-native-support/vendor/Common/include",
            "crates/ue4ssl-native-support/vendor/DynamicOutput/include",
            "crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include",
            "crates/ue4ssl-unreal-support/vendor/Unreal/include/Function",
            "crates/ue4ssl-hook/include",
        ],
    );

    require_paths_exist(
        "ue4ssl-javascript sources",
        javascript_sources(&workspace_root),
    );

    for tracked in [
        workspace_root.join("crates/ue4ssl-javascript/native/cpp"),
        workspace_root.join("crates/ue4ssl-javascript/native/include"),
        workspace_root.join("crates/ue4ssl-javascript-support/vendor/quickjs"),
        workspace_root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/include"),
        workspace_root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_include"),
        workspace_root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_src"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/generated_include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Input/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Common/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/DynamicOutput/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Function"),
        workspace_root.join("crates/ue4ssl-hook/include"),
    ] {
        emit_rerun_for_tree(&tracked);
    }

    let generated = generate_abi_headers(&out_dir).expect("failed to generate ABI headers");

    compile_javascript_archive(
        &workspace_root,
        &generated.ue4ssl_include,
        &generated.unreal_include,
        profile,
    );

    emit_dylib_link("winhttp");

    emit_dylib_link("winmm");

    let target_dir = target_dir(&workspace_root, profile);

    println!(
        "cargo:rustc-link-arg-cdylib={}",
        target_dir.join("UE4SSL.dll.lib").display()
    );

    let archive = cc_archive_path(&out_dir, "ue4ssl_javascript_mod_cpp");

    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&archive)
    );
}

fn compile_javascript_archive(
    workspace_root: &std::path::Path,

    generated_ue4ssl_include: &std::path::Path,

    generated_unreal_include: &std::path::Path,

    profile: BuildProfile,
) {
    let mut build = Build::new();

    build.cargo_metadata(false);

    apply_common_msvc_flags(&mut build, profile, true, "/std:c++23preview");

    apply_common_defines(&mut build, profile);

    for include in common_include_dirs(workspace_root).into_iter().chain([
        workspace_root.join("crates/ue4ssl-javascript/native/include"),
        workspace_root.join("crates/ue4ssl-javascript-support/vendor/quickjs"),
        workspace_root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/include"),
        workspace_root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_include"),
        generated_ue4ssl_include.to_path_buf(),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/generated_include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Core"),
        generated_unreal_include.to_path_buf(),
    ]) {
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
        "crates/ue4ssl-javascript/native/cpp/dllmain.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSRustBridge.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSMod.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSHook.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSFetch.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSTimer.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSMemory.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSFileIO.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSAudio.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp",
        "crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp",
    ]
    .into_iter()
    .map(|relative| workspace_root.join(relative))
    .collect()
}

fn common_include_dirs(workspace_root: &std::path::Path) -> Vec<PathBuf> {
    let mut dirs = vec![
        workspace_root.join("crates/ue4ssl-native-support/vendor/Input/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Common/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/DynamicOutput/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Function"),
        workspace_root.join("crates/ue4ssl-hook/include"),
    ];

    let unreal_src = workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/src");

    for source_dir in collect_sources(&unreal_src, &["cpp"])
        .into_iter()
        .filter_map(|path| path.parent().map(|parent| parent.to_path_buf()))
    {
        if !dirs.contains(&source_dir) {
            dirs.push(source_dir);
        }
    }

    dirs
}
