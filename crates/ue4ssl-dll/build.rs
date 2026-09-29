use std::env;
use std::path::{Path, PathBuf};

use cc::Build;
use ue4ssl_build::common::{
    add_defines, apply_common_defines, apply_common_msvc_flags, cc_archive_path, collect_sources,
    define, emit_dylib_link, emit_rerun_for_tree, generate_abi_headers,
    object_searcher_include_dir, platform_include_dirs, require_nonempty_sources,
    require_paths_exist, scanner_include_dir, support_archive_from_env, ue4ssl_cpp_root,
    ue4ssl_sdk_include_dirs, version_defines, whole_archive_flag, workspace_root_from_manifest_dir,
    BuildProfile, GeneratedAbiIncludeRoots,
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
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("missing OUT_DIR"));
    let profile = BuildProfile::from_env();
    let generated = generate_abi_headers(&out_dir).expect("failed to generate ABI headers");

    let core_archive = build_ue4ssl_cpp_support(&workspace_root, &out_dir, profile, &generated);

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

    let native_support_archive = support_archive_from_env("ue4ssl_platform", "native_archive");
    let unreal_archive = support_archive_from_env("ue4ssl_unreal_support", "archive");
    let object_searcher_archive = support_archive_from_env("ue4ssl_object_searcher", "archive");
    let scanner_archive = support_archive_from_env("patternsleuth_bind_native", "archive");
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&core_archive)
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
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&scanner_archive)
    );
}

fn build_ue4ssl_cpp_support(
    workspace_root: &Path,
    out_dir: &Path,
    profile: BuildProfile,
    generated: &GeneratedAbiIncludeRoots,
) -> PathBuf {
    let ue4ssl_root = ue4ssl_cpp_root(workspace_root);
    let core_src = ue4ssl_root.join("src");
    require_paths_exist(
        "ue4ssl-dll cpp inputs",
        [
            core_src.clone(),
            ue4ssl_root.join("generated_src").join("version.cache"),
        ],
    );

    for tracked in [core_src.clone(), ue4ssl_root.join("generated_src")] {
        emit_rerun_for_tree(&tracked);
    }

    let mut build = Build::new();
    build.cargo_metadata(false);
    apply_common_msvc_flags(&mut build, profile, true, "/std:c++23preview");
    apply_common_defines(&mut build, profile);

    let mut include_dirs = platform_include_dirs(workspace_root);
    include_dirs.extend(ue4ssl_sdk_include_dirs(workspace_root, generated));
    include_dirs.extend([
        scanner_include_dir(workspace_root),
        object_searcher_include_dir(workspace_root),
    ]);
    require_paths_exist("ue4ssl-dll cpp includes", &include_dirs);
    for include in include_dirs {
        emit_rerun_for_tree(&include);
        build.include(include);
    }

    add_defines(
        &mut build,
        &[
            ("RC_UE4SSL_EXPORTS", None),
            ("RC_UNREAL_EXPORTS", None),
            ("RC_FUNCTION_EXPORTS", None),
            ("RC_CONSTRUCTS_EXPORTS", None),
            ("RC_M_PROGRAM_EXPORTS", None),
            ("RC_UE4SS_HOOK_EXPORTS", None),
            ("RC_DYNAMIC_OUTPUT_BUILD_STATIC", None),
            ("RC_SINGLE_PASS_SIG_SCANNER_BUILD_STATIC", None),
            ("RC_INPUT_BUILD_STATIC", None),
            ("RC_HELPERS_BUILD_STATIC", None),
        ],
    );

    for (name, value) in version_defines(workspace_root, profile) {
        define(&mut build, &name, value.as_deref());
    }

    let sources: Vec<_> = collect_sources(&core_src, &["cpp"])
        .into_iter()
        .filter(|path| path.file_name().and_then(|name| name.to_str()) != Some("PakMount.cpp"))
        .collect();
    require_nonempty_sources("ue4ssl-dll cpp archive", &core_src, &sources);
    for source in sources {
        build.file(source);
    }

    build.compile("ue4ssl_core_cpp");

    cc_archive_path(out_dir, "ue4ssl_core_cpp")
}
