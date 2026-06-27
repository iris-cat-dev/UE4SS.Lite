#[path = "../build_support/common.rs"]
mod common;

use std::env;
use std::path::PathBuf;

use cc::Build;
use common::{
    add_defines, apply_common_defines, apply_common_msvc_flags, cc_archive_path, collect_sources,
    emit_rerun_for_tree, emit_static_archive_metadata, generate_abi_headers,
    native_support_include_dirs, require_nonempty_sources, require_paths_exist,
    unreal_base_include_dirs, unreal_include_dirs, unreal_root, workspace_root_from_manifest_dir,
    BuildProfile,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-object-searcher currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("missing OUT_DIR"));
    let profile = BuildProfile::from_env();
    let generated = generate_abi_headers(&out_dir).expect("failed to generate ABI headers");

    let source_root = manifest_dir.join("native").join("src");
    let include_root = manifest_dir.join("include");
    let unreal_root = unreal_root(&workspace_root);
    let include_dirs = object_searcher_include_dirs(&workspace_root, &generated.unreal_include);

    require_paths_exist(
        "ue4ssl-object-searcher inputs",
        [
            source_root.clone(),
            include_root.clone(),
            unreal_root.join("include"),
            unreal_root.join("generated_include"),
            workspace_root.join("crates/ue4ssl-native-support/vendor/Common/include"),
            workspace_root.join("crates/ue4ssl-hook/include"),
        ],
    );

    for tracked in [
        source_root.clone(),
        include_root,
        unreal_root.join("include"),
        unreal_root.join("generated_include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Common/include"),
        workspace_root.join("crates/ue4ssl-hook/include"),
    ] {
        emit_rerun_for_tree(&tracked);
    }

    let mut build = Build::new();
    build.cargo_metadata(false);
    apply_common_msvc_flags(&mut build, profile, true, "/std:c++20");
    apply_common_defines(&mut build, profile);
    build.flag("/Od");
    build.flag("/Ob0");
    build.opt_level(0);

    for include in include_dirs {
        build.include(include);
    }

    add_defines(
        &mut build,
        &[
            ("RC_DYNAMIC_OUTPUT_EXPORTS", None),
            ("RC_SINGLE_PASS_SIG_SCANNER_EXPORTS", None),
            ("RC_CONSTRUCTS_EXPORTS", None),
            ("RC_HELPERS_EXPORTS", None),
            ("RC_FUNCTION_EXPORTS", None),
            ("RC_ASM_HELPER_EXPORTS", None),
            ("RC_UE4SS_HOOK_EXPORTS", None),
            ("RC_UNREAL_BUILD_STATIC", None),
            ("RC_UNREAL_EXPORTS", None),
        ],
    );

    let sources = collect_sources(&source_root, &["cpp"]);
    require_nonempty_sources(
        "ue4ssl-object-searcher native archive",
        &source_root,
        &sources,
    );
    for source in sources {
        build.file(source);
    }

    build.compile("ue4ssl_object_searcher_cpp");

    let archive = cc_archive_path(&out_dir, "ue4ssl_object_searcher_cpp");
    emit_static_archive_metadata("archive", &archive);
}

fn object_searcher_include_dirs(
    workspace_root: &std::path::Path,
    generated_unreal_include: &std::path::Path,
) -> Vec<PathBuf> {
    let mut dirs = native_support_include_dirs(workspace_root);
    dirs.extend(unreal_base_include_dirs(workspace_root));
    dirs.extend(unreal_include_dirs(
        workspace_root,
        Some(generated_unreal_include),
    ));
    dirs
}
