use std::env;
use std::path::{Path, PathBuf};

use cc::Build;
use ue4ssl_build::common::{
    apply_common_defines, apply_common_msvc_flags, cc_archive_path, collect_sources,
    emit_core_import_library_link, emit_rerun_for_tree, generate_abi_headers,
    object_searcher_include_dir, platform_include_dirs, require_nonempty_sources,
    require_paths_exist, require_relative_paths_exist, scanner_include_dir,
    ue4ssl_sdk_include_dirs, version_defines, whole_archive_flag, workspace_root_from_manifest_dir,
    BuildProfile,
};

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-lua currently supports only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("missing OUT_DIR"));
    let profile = BuildProfile::from_env();

    require_relative_paths_exist(
        "ue4ssl-lua",
        &workspace_root,
        &[
            "scripts/ue4ssl-lua/native/cpp",
            "scripts/ue4ssl-lua/native/include",
            "scripts/ue4ssl-lua-support/vendor/LuaMadeSimple/include",
            "scripts/ue4ssl-lua-support/vendor/LuaRaw/include",
            "crates/ue4ssl-dll/native/UE4SSL/generated_src/version.cache",
        ],
    );
    require_paths_exist("ue4ssl-lua sources", lua_sources(&workspace_root));

    for tracked in [
        workspace_root.join("scripts/ue4ssl-lua/native/cpp"),
        workspace_root.join("scripts/ue4ssl-lua/native/include"),
        workspace_root.join("scripts/ue4ssl-lua-support/vendor/LuaMadeSimple/include"),
        workspace_root.join("scripts/ue4ssl-lua-support/vendor/LuaRaw/include"),
        workspace_root.join("crates/ue4ssl-dll/native/UE4SSL/generated_src"),
    ] {
        emit_rerun_for_tree(&tracked);
    }

    let generated = generate_abi_headers(&out_dir).expect("failed to generate ABI headers");
    let mut include_dirs = ue4ssl_sdk_include_dirs(&workspace_root, &generated);
    include_dirs.extend(platform_include_dirs(&workspace_root));
    include_dirs.extend([
        scanner_include_dir(&workspace_root),
        object_searcher_include_dir(&workspace_root),
        workspace_root.join("scripts/ue4ssl-lua/native/include"),
        workspace_root.join("scripts/ue4ssl-lua/native/cpp"),
        workspace_root.join("scripts/ue4ssl-lua-support/vendor/LuaMadeSimple/include"),
        workspace_root.join("scripts/ue4ssl-lua-support/vendor/LuaRaw/include"),
    ]);
    require_paths_exist("ue4ssl-lua includes", &include_dirs);
    for include in &include_dirs {
        emit_rerun_for_tree(include);
    }
    compile_lua_archive(&workspace_root, &include_dirs, profile);

    emit_core_import_library_link(&workspace_root, profile).expect("failed to link UE4SSL core");

    let archive = cc_archive_path(&out_dir, "ue4ssl_lua_mod_cpp");
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&archive)
    );
}

fn compile_lua_archive(workspace_root: &Path, include_dirs: &[PathBuf], profile: BuildProfile) {
    let mut build = Build::new();
    apply_common_msvc_flags(&mut build, profile, true, "/std:c++23preview");
    apply_common_defines(&mut build, profile);
    build.warnings(false);

    for include in include_dirs {
        build.include(include);
    }

    build.define("RC_LUA_MADE_SIMPLE_BUILD_STATIC", None);
    build.define("RC_LUA_RAW_BUILD_STATIC", None);

    for (name, value) in version_defines(workspace_root, profile) {
        build.define(&name, value.as_deref());
    }

    for source in lua_sources(workspace_root) {
        build.file(source);
    }

    build.compile("ue4ssl_lua_mod_cpp");
}

fn lua_sources(workspace_root: &Path) -> Vec<PathBuf> {
    let mut sources: Vec<PathBuf> = [
        "scripts/ue4ssl-lua/native/cpp/dllmain.cpp",
        "scripts/ue4ssl-lua/native/cpp/LuaCompat.cpp",
        "scripts/ue4ssl-lua/native/cpp/LuaEngineRegistry.cpp",
        "scripts/ue4ssl-lua/native/cpp/LuaLibrary.cpp",
        "scripts/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp",
    ]
    .into_iter()
    .map(|relative| workspace_root.join(relative))
    .collect();

    let lua_type_dir = workspace_root
        .join("scripts/ue4ssl-lua/native/cpp")
        .join("LuaType");
    let lua_type_sources = collect_sources(&lua_type_dir, &["cpp"]);
    require_nonempty_sources(
        "ue4ssl-lua LuaType sources",
        &lua_type_dir,
        &lua_type_sources,
    );
    sources.extend(lua_type_sources);
    sources.sort();
    sources
}
