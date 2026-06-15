use std::env;
use std::ffi::OsStr;
use std::fs;
use std::path::{Path, PathBuf};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-native-support currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = manifest_dir
        .parent()
        .and_then(Path::parent)
        .expect("crate path should be <workspace>/crates/<name>")
        .to_path_buf();

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

    require_paths_exist("ue4ssl-native-support sources", sources.iter());
    require_paths_exist("ue4ssl-native-support includes", includes.iter());

    let tracked_dirs = [
        workspace_root.join("crates/ue4ssl-native-support/vendor/Input/src"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Input/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Common/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/DynamicOutput/src"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/DynamicOutput/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/SinglePassSigScanner/src"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include"),
        workspace_root.join("crates/ue4ssl-hook/include"),
    ];

    for dir in tracked_dirs {
        emit_rerun_for_tree(&dir);
    }

    println!("cargo:rustc-link-lib=user32");
    println!("cargo:rustc-link-lib=kernel32");
    println!("cargo:rustc-link-lib=psapi");

    let cargo_profile = env::var("PROFILE").unwrap_or_default();
    let is_debug_profile = cargo_profile != "release";

    let mut build = cc::Build::new();
    build.cpp(true);
    build.static_crt(false);
    build.debug(is_debug_profile);
    build.flag("-MD");

    for include in includes {
        build.include(include);
    }

    for source in sources {
        build.file(source);
    }

    for define in [
        "_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR=1",
        "RC_INPUT_EXPORTS",
        "RC_INPUT_BUILD_STATIC",
        "RC_HELPERS_EXPORTS",
        "RC_HELPERS_BUILD_STATIC",
        "RC_DYNAMIC_OUTPUT_EXPORTS",
        "RC_DYNAMIC_OUTPUT_BUILD_STATIC",
        "RC_ASM_HELPER_EXPORTS",
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
}

fn require_paths_exist<'a, I>(component: &str, paths: I)
where
    I: IntoIterator<Item = &'a PathBuf>,
{
    let mut missing: Vec<String> = paths
        .into_iter()
        .filter(|path| !path.exists())
        .map(|path| path.display().to_string())
        .collect();

    if missing.is_empty() {
        return;
    }

    missing.sort();
    panic!(
        "{component} is missing required build input(s):\n  - {}\n\nRestore the missing source tree/submodule or remove the artifact from the active build baseline.",
        missing.join("\n  - ")
    );
}

fn emit_rerun_for_tree(path: &Path) {
    if path.is_file() {
        println!("cargo:rerun-if-changed={}", path.display());
        return;
    }

    if !path.is_dir() {
        return;
    }

    let mut entries: Vec<PathBuf> = fs::read_dir(path)
        .unwrap_or_else(|err| panic!("failed to read {}: {err}", path.display()))
        .map(|entry| {
            entry
                .unwrap_or_else(|err| panic!("failed to read entry in {}: {err}", path.display()))
                .path()
        })
        .collect();
    entries.sort();

    for entry in entries {
        if entry.is_dir() {
            emit_rerun_for_tree(&entry);
        } else if matches!(
            entry.extension().and_then(OsStr::to_str),
            Some("c" | "cc" | "cpp" | "h" | "hpp")
        ) {
            println!("cargo:rerun-if-changed={}", entry.display());
        }
    }
}
