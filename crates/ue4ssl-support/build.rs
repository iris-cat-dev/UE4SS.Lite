#[path = "../build_support/common.rs"]
mod common;

use std::env;
use std::path::{Path, PathBuf};

use cc::Build;
use common::{
    add_defines, apply_common_defines, apply_common_msvc_flags, cc_archive_path, collect_sources,
    common_native_include_dirs, define, emit_rerun_for_tree, emit_static_archive_metadata,
    generate_abi_headers, require_nonempty_sources, require_paths_exist, ue4ssl_cpp_root,
    version_defines, workspace_root_from_manifest_dir, BuildProfile, GeneratedAbiIncludeRoots,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-support currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("missing OUT_DIR"));
    let profile = BuildProfile::from_env();
    let generated = generate_abi_headers(&out_dir).expect("failed to generate ABI headers");

    build_native_support(&workspace_root, &out_dir, profile);
    build_ue4ssl_cpp_support(&workspace_root, &out_dir, profile, &generated);
}

fn build_native_support(workspace_root: &Path, out_dir: &Path, profile: BuildProfile) {
    let sources = [
        workspace_root.join("crates/ue4ssl-native-support/vendor/Input/src/Handler.cpp"),
        workspace_root
            .join("crates/ue4ssl-native-support/vendor/DynamicOutput/src/DebugConsoleDevice.cpp"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/DynamicOutput/src/Output.cpp"),
        workspace_root
            .join("crates/ue4ssl-native-support/vendor/DynamicOutput/src/OutputDevice.cpp"),
        workspace_root.join(
            "crates/ue4ssl-native-support/vendor/SinglePassSigScanner/src/SinglePassScannerShim.cpp",
        ),
    ];

    let includes = [
        workspace_root.join("crates/ue4ssl-native-support/vendor/Input/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Common/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/DynamicOutput/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include"),
        workspace_root.join("crates/ue4ssl-hook/include"),
    ];

    require_paths_exist("ue4ssl-support native sources", sources.iter());
    require_paths_exist("ue4ssl-support native includes", includes.iter());

    for tracked in [
        workspace_root.join("crates/ue4ssl-native-support/vendor/Input/src"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Input/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Common/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/DynamicOutput/src"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/DynamicOutput/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/SinglePassSigScanner/src"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include"),
        workspace_root.join("crates/ue4ssl-hook/include"),
    ] {
        emit_rerun_for_tree(&tracked);
    }

    println!("cargo:rustc-link-lib=user32");
    println!("cargo:rustc-link-lib=kernel32");
    println!("cargo:rustc-link-lib=psapi");

    let mut build = Build::new();
    build.cargo_metadata(false);
    build.cpp(true);
    build.static_crt(false);
    build.debug(profile.is_debug());
    build.flag("-MD");

    for include in includes {
        build.include(include);
    }

    for source in sources {
        build.file(source);
    }

    for define in [
        "_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR=1",
        "RC_INPUT_BUILD_STATIC",
        "RC_HELPERS_BUILD_STATIC",
        "RC_DYNAMIC_OUTPUT_EXPORTS",
        "RC_DYNAMIC_OUTPUT_BUILD_STATIC",
        "RC_ASM_HELPER_BUILD_STATIC",
        "RC_SINGLE_PASS_SIG_SCANNER_EXPORTS",
        "RC_SINGLE_PASS_SIG_SCANNER_BUILD_STATIC",
        "RC_STRING_EXPORTS",
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

    build.compile("ue4ssl_native_support_cpp");

    let archive = cc_archive_path(out_dir, "ue4ssl_native_support_cpp");
    emit_static_archive_metadata("native_archive", &archive);
}

fn build_ue4ssl_cpp_support(
    workspace_root: &Path,
    out_dir: &Path,
    profile: BuildProfile,
    generated: &GeneratedAbiIncludeRoots,
) {
    let ue4ssl_root = ue4ssl_cpp_root(workspace_root);
    let core_src = ue4ssl_root.join("src");
    require_paths_exist(
        "ue4ssl-support cpp inputs",
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
        workspace_root,
        Some(&generated.ue4ssl_include),
        Some(&generated.unreal_include),
    ) {
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
    require_nonempty_sources("ue4ssl-support cpp archive", &core_src, &sources);
    for source in sources {
        build.file(source);
    }

    build.compile("ue4ssl_core_cpp");

    let archive = cc_archive_path(out_dir, "ue4ssl_core_cpp");
    emit_static_archive_metadata("archive", &archive);
}
