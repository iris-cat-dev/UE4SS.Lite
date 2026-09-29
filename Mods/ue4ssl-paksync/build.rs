use std::env;
use std::path::{Path, PathBuf};

use cc::Build;
use ue4ssl_build::common::{
    apply_common_defines, apply_common_msvc_flags, cc_archive_path, collect_sources,
    emit_core_import_library_link, emit_dylib_link, emit_rerun_for_tree, generate_abi_headers,
    object_searcher_include_dir, platform_include_dirs, require_nonempty_sources,
    require_paths_exist, require_relative_paths_exist, scanner_include_dir,
    ue4ssl_sdk_include_dirs, unreal_source_include_dirs, version_defines, whole_archive_flag,
    workspace_root_from_manifest_dir, BuildProfile,
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
            "Mods/ue4ssl-paksync/native/cpp",
            "Mods/ue4ssl-paksync/native/include",
            "crates/ue4ssl-dll/native/UE4SSL/generated_src/version.cache",
        ],
    );

    let sources = paksync_sources(&workspace_root);
    require_paths_exist("ue4ssl-paksync sources", &sources);

    for tracked in [
        workspace_root.join("Mods/ue4ssl-paksync/native/cpp"),
        workspace_root.join("Mods/ue4ssl-paksync/native/include"),
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
        workspace_root.join("Mods/ue4ssl-paksync/native/include"),
        workspace_root.join("Mods/ue4ssl-paksync/native/cpp"),
    ]);
    include_dirs.extend(unreal_source_include_dirs(&workspace_root));
    require_paths_exist("ue4ssl-paksync includes", &include_dirs);
    for include in &include_dirs {
        emit_rerun_for_tree(include);
    }
    compile_paksync_archive(&workspace_root, &include_dirs, profile, &sources);

    emit_dylib_link("bcrypt");
    emit_dylib_link("version");
    emit_dylib_link("user32");
    emit_dylib_link("shell32");
    emit_dylib_link("comctl32");
    emit_dylib_link("gdi32");

    emit_core_import_library_link(&workspace_root, profile).expect("failed to link UE4SSL core");

    let archive = cc_archive_path(&out_dir, "ue4ssl_paksync_mod_cpp");
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&archive)
    );
}

fn compile_paksync_archive(
    workspace_root: &Path,
    include_dirs: &[PathBuf],
    profile: BuildProfile,
    sources: &[PathBuf],
) {
    let mut build = Build::new();
    build.cargo_metadata(false);
    apply_common_msvc_flags(&mut build, profile, true, "/std:c++23preview");
    apply_common_defines(&mut build, profile);
    build.warnings(false);

    for include in include_dirs {
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
    let source_dir = workspace_root.join("Mods/ue4ssl-paksync/native/cpp");
    let sources = collect_sources(&source_dir, &["cpp"]);
    require_nonempty_sources("ue4ssl-paksync C++ sources", &source_dir, &sources);
    sources
}
