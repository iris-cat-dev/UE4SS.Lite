use std::env;
use std::path::{Path, PathBuf};

use cc::Build;
use ue4ssl_build::common::{
    cc_archive_path, emit_rerun_for_tree, emit_static_archive_metadata, generate_abi_headers,
    require_paths_exist, workspace_root_from_manifest_dir, BuildProfile, GeneratedAbiIncludeRoots,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-platform currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("missing OUT_DIR"));
    let profile = BuildProfile::from_env();
    let generated = generate_abi_headers(&out_dir).expect("failed to generate ABI headers");

    build_native_support(&workspace_root, &out_dir, profile, &generated);
}

fn build_native_support(
    workspace_root: &Path,
    out_dir: &Path,
    profile: BuildProfile,
    generated: &GeneratedAbiIncludeRoots,
) {
    let sources = [
        workspace_root.join("crates/ue4ssl-platform/native/Input/src/Handler.cpp"),
        workspace_root
            .join("crates/ue4ssl-platform/native/DynamicOutput/src/DebugConsoleDevice.cpp"),
        workspace_root.join("crates/ue4ssl-platform/native/DynamicOutput/src/Output.cpp"),
        workspace_root.join("crates/ue4ssl-platform/native/DynamicOutput/src/OutputDevice.cpp"),
    ];

    let includes = [
        workspace_root.join("crates/ue4ssl-platform/native/Input/include"),
        workspace_root.join("crates/ue4ssl-platform/native/Common/include"),
        workspace_root.join("crates/ue4ssl-platform/native/DynamicOutput/include"),
    ];

    require_paths_exist("ue4ssl-platform native sources", sources.iter());
    require_paths_exist("ue4ssl-platform native includes", includes.iter());

    for tracked in [
        workspace_root.join("crates/ue4ssl-platform/native/Input/src"),
        workspace_root.join("crates/ue4ssl-platform/native/Input/include"),
        workspace_root.join("crates/ue4ssl-platform/native/Common/include"),
        workspace_root.join("crates/ue4ssl-platform/native/DynamicOutput/src"),
        workspace_root.join("crates/ue4ssl-platform/native/DynamicOutput/include"),
    ] {
        emit_rerun_for_tree(&tracked);
    }

    println!("cargo:rustc-link-lib=user32");
    println!("cargo:rustc-link-lib=kernel32");

    let mut build = Build::new();
    build.cargo_metadata(false);
    build.cpp(true);
    build.static_crt(false);
    build.debug(profile.is_debug());
    build.flag("-MD");
    build.include(&generated.ue4ssl_include);

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
