use std::env;
use std::ffi::OsStr;
use std::path::{Path, PathBuf};
use ue4ssl_build::common::{
    collect_sources_in_dir, emit_rerun_for_tree, require_nonempty_sources, require_paths_exist,
    workspace_root_from_manifest_dir, BuildProfile, UE4SSL_NATIVE_COMMON_INCLUDE_ROOT,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-lua-support currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);

    let lua_raw_dir = workspace_root.join("scripts/ue4ssl-lua-support/vendor/LuaRaw");
    let lua_made_simple_dir =
        workspace_root.join("scripts/ue4ssl-lua-support/vendor/LuaMadeSimple");
    let common_include = workspace_root.join(UE4SSL_NATIVE_COMMON_INCLUDE_ROOT);

    require_paths_exist(
        "ue4ssl-lua-support",
        [&lua_raw_dir, &lua_made_simple_dir, &common_include],
    );

    emit_rerun_for_tree(&lua_raw_dir);
    emit_rerun_for_tree(&lua_made_simple_dir);
    emit_rerun_for_tree(&common_include);

    let is_debug_profile = BuildProfile::from_env().is_debug();

    compile_lua_raw(&lua_raw_dir, is_debug_profile);
    compile_lua_made_simple(
        &lua_raw_dir,
        &lua_made_simple_dir,
        &common_include,
        is_debug_profile,
    );
}

fn compile_lua_raw(lua_raw_dir: &Path, is_debug_profile: bool) {
    let source_dir = lua_raw_dir.join("src");
    let mut sources = collect_sources_in_dir(&source_dir, &["c"]);
    sources.retain(|path| {
        !matches!(
            path.file_name().and_then(OsStr::to_str),
            Some("lua.c" | "luac.c")
        )
    });
    require_nonempty_sources("ue4ssl-lua-support LuaRaw", &source_dir, &sources);

    let mut build = cc::Build::new();
    build.cpp(false);
    build.static_crt(false);
    build.debug(is_debug_profile);
    build.flag("-MD");
    build.flag("/utf-8");
    build.flag("/MP");
    build.flag("/W0");
    build.include(lua_raw_dir.join("include"));
    build.define("RC_LUA_RAW_EXPORTS", None);
    build.define("RC_LUA_RAW_BUILD_STATIC", None);

    for source in sources {
        build.file(source);
    }

    build.compile("ue4ssl_lua_raw");
}

fn compile_lua_made_simple(
    lua_raw_dir: &Path,
    lua_made_simple_dir: &Path,
    common_include: &Path,
    is_debug_profile: bool,
) {
    let source_dir = lua_made_simple_dir.join("src");
    let sources = collect_sources_in_dir(&source_dir, &["cpp"]);
    require_nonempty_sources("ue4ssl-lua-support LuaMadeSimple", &source_dir, &sources);

    let mut build = cc::Build::new();
    build.cpp(true);
    build.static_crt(false);
    build.debug(is_debug_profile);
    build.flag("-MD");
    build.flag("/utf-8");
    build.flag("/MP");
    build.flag("/EHsc");
    build.flag("/std:c++23preview");
    build.flag("/bigobj");
    build.warnings(false);
    build.include(lua_made_simple_dir.join("include"));
    build.include(lua_raw_dir.join("include"));
    build.include(common_include);
    build.define("RC_LUA_MADE_SIMPLE_EXPORTS", None);
    build.define("RC_LUA_MADE_SIMPLE_BUILD_STATIC", None);

    for source in sources {
        build.file(source);
    }

    build.compile("ue4ssl_lua_made_simple");
}
