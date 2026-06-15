use std::env;
use std::ffi::OsStr;
use std::fs;
use std::path::{Path, PathBuf};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-lua-support currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = manifest_dir
        .parent()
        .and_then(Path::parent)
        .expect("crate path should be <workspace>/crates/<name>")
        .to_path_buf();

    let lua_raw_dir = workspace_root.join("crates/ue4ssl-lua-support/vendor/LuaRaw");
    let lua_made_simple_dir = workspace_root.join("crates/ue4ssl-lua-support/vendor/LuaMadeSimple");
    let common_include = workspace_root.join("crates/ue4ssl-native-support/vendor/Common/include");

    require_path_exists("ue4ssl-lua-support", &lua_raw_dir);
    require_path_exists("ue4ssl-lua-support", &lua_made_simple_dir);
    require_path_exists("ue4ssl-lua-support", &common_include);

    emit_rerun_for_tree(&lua_raw_dir);
    emit_rerun_for_tree(&lua_made_simple_dir);
    emit_rerun_for_tree(&common_include);

    let cargo_profile = env::var("PROFILE").unwrap_or_default();
    let is_debug_profile = cargo_profile != "release";

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
    let mut sources = collect_sources(&source_dir, &["c"]);
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
    let sources = collect_sources(&source_dir, &["cpp"]);
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

fn require_path_exists(component: &str, path: &Path) {
    if path.exists() {
        return;
    }

    panic!(

        "{component} is missing required build input:\n  - {}\n\nRestore the missing source tree/submodule or remove the artifact from the active build baseline.",

        path.display()

    );
}

fn require_nonempty_sources(component: &str, source_root: &Path, sources: &[PathBuf]) {
    if sources.is_empty() {
        panic!(

            "{component} did not find any source files under {}.\n\nRestore the missing source tree/submodule or remove the artifact from the active build baseline.",

            source_root.display()

        );
    }
}

fn collect_sources(dir: &Path, extensions: &[&str]) -> Vec<PathBuf> {
    if !dir.is_dir() {
        return Vec::new();
    }

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
        } else {
            println!("cargo:rerun-if-changed={}", entry.display());
        }
    }
}
