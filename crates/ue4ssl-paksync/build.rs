
use std::env;
use std::path::{Path, PathBuf};

use cc::Build;
use ue4ssl_build::common::{
    apply_common_defines, apply_common_msvc_flags, cc_archive_path, collect_sources,
    emit_dylib_link, emit_rerun_for_tree, generate_abi_headers, require_nonempty_sources,
    require_paths_exist, require_relative_paths_exist, target_dir, version_defines,
    whole_archive_flag, workspace_root_from_manifest_dir, BuildProfile,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-paksync currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("missing OUT_DIR"));
    let profile = BuildProfile::from_env();

    require_relative_paths_exist(
        "ue4ssl-paksync",
        &workspace_root,
        &[
            "crates/ue4ssl-paksync/native/cpp",
            "crates/ue4ssl-paksync/native/include",
            "crates/ue4ssl-dll/native/UE4SSL/include",
            "crates/ue4ssl-dll/native/UE4SSL/generated_include",
            "crates/ue4ssl-dll/native/UE4SSL/generated_src/version.cache",
            "crates/ue4ssl-unreal-support/vendor/Unreal/include",
            "crates/ue4ssl-unreal-support/vendor/Unreal/generated_include",
            "crates/ue4ssl-object-searcher/include",
            "crates/ue4ssl-platform/native/Input/include",
            "crates/ue4ssl-platform/native/Common/include",
            "crates/ue4ssl-platform/native/DynamicOutput/include",
            "crates/patternsleuth-bind/native/SinglePassSigScanner/include",
            "crates/ue4ssl-unreal-support/vendor/Unreal/include/Function",
            "crates/ue4ssl-hook/include",
        ],
    );

    let sources = paksync_sources(&workspace_root);
    require_paths_exist("ue4ssl-paksync sources", &sources);

    for tracked in [
        workspace_root.join("crates/ue4ssl-paksync/native/cpp"),
        workspace_root.join("crates/ue4ssl-paksync/native/include"),
        workspace_root.join("crates/ue4ssl-dll/native/UE4SSL/include"),
        workspace_root.join("crates/ue4ssl-dll/native/UE4SSL/generated_include"),
        workspace_root.join("crates/ue4ssl-dll/native/UE4SSL/generated_src"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/generated_include"),
        workspace_root.join("crates/ue4ssl-object-searcher/include"),
        workspace_root.join("crates/ue4ssl-platform/native/Input/include"),
        workspace_root.join("crates/ue4ssl-platform/native/Common/include"),
        workspace_root.join("crates/ue4ssl-platform/native/DynamicOutput/include"),
        workspace_root.join("crates/patternsleuth-bind/native/SinglePassSigScanner/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Function"),
        workspace_root.join("crates/ue4ssl-hook/include"),
    ] {
        emit_rerun_for_tree(&tracked);
    }

    let generated = generate_abi_headers(&out_dir).expect("failed to generate ABI headers");
    compile_paksync_archive(
        &workspace_root,
        &generated.ue4ssl_include,
        &generated.unreal_include,
        profile,
        &sources,
    );

    emit_dylib_link("bcrypt");
    emit_dylib_link("version");
    emit_dylib_link("user32");
    emit_dylib_link("shell32");
    emit_dylib_link("comctl32");
    emit_dylib_link("gdi32");

    let target_dir = target_dir(&workspace_root, profile);
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        target_dir.join("UE4SSL.dll.lib").display()
    );

    let archive = cc_archive_path(&out_dir, "ue4ssl_paksync_mod_cpp");
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&archive)
    );
}

fn compile_paksync_archive(
    workspace_root: &Path,
    generated_ue4ssl_include: &Path,
    generated_unreal_include: &Path,
    profile: BuildProfile,
    sources: &[PathBuf],
) {
    let mut build = Build::new();
    build.cargo_metadata(false);
    apply_common_msvc_flags(&mut build, profile, true, "/std:c++23preview");
    apply_common_defines(&mut build, profile);
    build.warnings(false);

    for include in common_include_dirs(workspace_root).into_iter().chain([
        workspace_root.join("crates/ue4ssl-paksync/native/include"),
        workspace_root.join("crates/ue4ssl-paksync/native/cpp"),
        workspace_root.join("crates/ue4ssl-dll/native/UE4SSL/include"),
        workspace_root.join("crates/ue4ssl-dll/native/UE4SSL/generated_include"),
        generated_ue4ssl_include.to_path_buf(),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include"),
        workspace_root.join("crates/ue4ssl-object-searcher/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/generated_include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Core"),
        generated_unreal_include.to_path_buf(),
    ]) {
        build.include(include);
    }

    build.define("RC_UE4SS_HOOK_BUILD_STATIC", None);

    for (name, value) in version_defines(workspace_root, profile) {
        build.define(&name, value.as_deref());
    }

    for source in sources {
        build.file(source);
    }

    build.compile("ue4ssl_paksync_mod_cpp");
}

fn paksync_sources(workspace_root: &Path) -> Vec<PathBuf> {
    let source_dir = workspace_root.join("crates/ue4ssl-paksync/native/cpp");
    let sources = collect_sources(&source_dir, &["cpp"]);
    require_nonempty_sources("ue4ssl-paksync C++ sources", &source_dir, &sources);
    sources
}

fn common_include_dirs(workspace_root: &Path) -> Vec<PathBuf> {
    let mut dirs = vec![
        workspace_root.join("crates/ue4ssl-platform/native/Input/include"),
        workspace_root.join("crates/ue4ssl-platform/native/Common/include"),
        workspace_root.join("crates/ue4ssl-platform/native/DynamicOutput/include"),
        workspace_root.join("crates/patternsleuth-bind/native/SinglePassSigScanner/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Function"),
        workspace_root.join("crates/ue4ssl-object-searcher/include"),
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
