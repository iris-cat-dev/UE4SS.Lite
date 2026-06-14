use std::env;

use std::ffi::OsStr;

use std::fs;

use std::path::{Path, PathBuf};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();

    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();

    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-javascript-support currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));

    let workspace_root = manifest_dir
        .parent()
        .and_then(Path::parent)
        .expect("crate path should be <workspace>/crates/<name>")
        .to_path_buf();

    let quickjs_dir = workspace_root.join("crates/ue4ssl-javascript-support/vendor/quickjs");

    require_path_exists("ue4ssl-javascript-support", &quickjs_dir);

    emit_rerun_for_tree(&quickjs_dir);

    let cargo_profile = env::var("PROFILE").unwrap_or_default();

    let is_debug_profile = cargo_profile != "release";

    let sources = collect_sources(&quickjs_dir, &["c"]);

    if sources.is_empty() {
        panic!(

            "ue4ssl-javascript-support did not find any QuickJS C sources under {}.\n\nRestore the missing source tree/submodule or remove the artifact from the active build baseline.",

            quickjs_dir.display()

        );
    }

    let mut build = cc::Build::new();

    build.cpp(false);

    build.static_crt(false);

    build.debug(is_debug_profile);

    build.flag("-MD");

    build.include(&quickjs_dir);

    build.warnings(false);

    build.flag("/utf-8");

    build.flag("/MP");

    build.define("CONFIG_VERSION", Some("\"2024-01-13\""));

    build.define("_GNU_SOURCE", None);

    for source in sources {
        build.file(source);
    }

    build.compile("ue4ssl_javascript_support_c");
}

fn require_path_exists(component: &str, path: &Path) {
    if path.exists() {
        return;
    }

    panic!(

        "{component} is missing required build input:\n  - {}\n\nRestore the missing source tree/submodule or remove the artifact from the active build baseline.",

        path.display()

    );
}

fn collect_sources(dir: &Path, extensions: &[&str]) -> Vec<PathBuf> {
    let mut entries: Vec<PathBuf> = fs::read_dir(dir)
        .unwrap_or_else(|err| panic!("failed to read {}: {err}", dir.display()))
        .map(|entry| {
            entry
                .unwrap_or_else(|err| panic!("failed to read entry in {}: {err}", dir.display()))
                .path()
        })
        .filter(|path| {
            path.is_file()
                && matches!(

                    path.extension().and_then(OsStr::to_str),

                    Some(extension) if extensions.contains(&extension)

                )
        })
        .collect();

    entries.sort();

    entries
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
        } else if matches!(entry.extension().and_then(OsStr::to_str), Some("c" | "h")) {
            println!("cargo:rerun-if-changed={}", entry.display());
        }
    }
}
