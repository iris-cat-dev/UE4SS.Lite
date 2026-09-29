use std::env;

use std::path::PathBuf;
use ue4ssl_build::common::{
    collect_sources_in_dir, emit_rerun_for_tree, require_nonempty_sources, require_paths_exist,
    workspace_root_from_manifest_dir, BuildProfile,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();

    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();

    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-javascript-support currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));

    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);

    let quickjs_dir = workspace_root.join("scripts/ue4ssl-javascript-support/vendor/quickjs");

    require_paths_exist("ue4ssl-javascript-support", [&quickjs_dir]);

    emit_rerun_for_tree(&quickjs_dir);

    let is_debug_profile = BuildProfile::from_env().is_debug();

    let sources = collect_sources_in_dir(&quickjs_dir, &["c"]);

    require_nonempty_sources("ue4ssl-javascript-support QuickJS", &quickjs_dir, &sources);

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
