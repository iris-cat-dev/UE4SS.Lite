#[path = "../build_support/common.rs"]
mod common;

use std::env;
use std::path::PathBuf;

use cc::Build;
use common::{
    add_defines, apply_common_defines, apply_common_msvc_flags, cc_archive_path, collect_sources,
    common_native_include_dirs, define, emit_rerun_for_tree, emit_static_archive_metadata,
    generate_abi_headers, require_nonempty_sources, require_paths_exist, ue4ssl_cpp_root,
    version_defines, workspace_root_from_manifest_dir, BuildProfile,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-cpp-support currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("missing OUT_DIR"));
    let profile = BuildProfile::from_env();
    let generated = generate_abi_headers(&out_dir).expect("failed to generate ABI headers");

    let ue4ssl_root = ue4ssl_cpp_root(&workspace_root);
    let core_src = ue4ssl_root.join("src");
    require_paths_exist(
        "ue4ssl-cpp-support inputs",
        [
            core_src.clone(),
            ue4ssl_root.join("include"),
            ue4ssl_root.join("generated_include"),
            ue4ssl_root.join("generated_src").join("version.cache"),
        ],
    );

    for tracked in [
        core_src.clone(),
        ue4ssl_root.join("include"),
        ue4ssl_root.join("generated_include"),
        ue4ssl_root.join("generated_src"),
    ] {
        emit_rerun_for_tree(&tracked);
    }

    let mut build = Build::new();
    build.cargo_metadata(false);
    apply_common_msvc_flags(&mut build, profile, true, "/std:c++23preview");
    apply_common_defines(&mut build, profile);

    for include in common_native_include_dirs(
        &workspace_root,
        Some(&generated.ue4ssl_include),
        Some(&generated.unreal_include),
    ) {
        build.include(include);
    }

    add_defines(
        &mut build,
        &[
            ("RC_UE4SSL_EXPORTS", None),
            ("RC_DYNAMIC_OUTPUT_EXPORTS", None),
            ("RC_UNREAL_EXPORTS", None),
            ("RC_SINGLE_PASS_SIG_SCANNER_EXPORTS", None),
            ("RC_FUNCTION_EXPORTS", None),
            ("RC_INPUT_EXPORTS", None),
            ("RC_CONSTRUCTS_EXPORTS", None),
            ("RC_HELPERS_EXPORTS", None),
            ("RC_M_PROGRAM_EXPORTS", None),
            ("RC_UE4SS_HOOK_EXPORTS", None),
        ],
    );

    for (name, value) in version_defines(&workspace_root, profile) {
        define(&mut build, &name, value.as_deref());
    }

    let sources: Vec<_> = collect_sources(&core_src, &["cpp"])
        .into_iter()
        .filter(|path| path.file_name().and_then(|name| name.to_str()) != Some("PakMount.cpp"))
        .collect();
    require_nonempty_sources("ue4ssl-cpp-support core archive", &core_src, &sources);
    for source in sources {
        build.file(source);
    }

    build.compile("ue4ssl_core_cpp");

    let archive = cc_archive_path(&out_dir, "ue4ssl_core_cpp");
    emit_static_archive_metadata("archive", &archive);
}
