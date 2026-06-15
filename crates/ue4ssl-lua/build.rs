#[path = "../build_support/common.rs"]
mod common;

use std::env;
use std::path::{Path, PathBuf};

use cc::Build;
use common::{
    apply_common_defines, apply_common_msvc_flags, cc_archive_path, collect_sources,
    emit_rerun_for_tree, generate_abi_headers, require_nonempty_sources, require_paths_exist,
    require_relative_paths_exist, target_dir, version_defines, whole_archive_flag,
    workspace_root_from_manifest_dir, BuildProfile,
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
            "crates/ue4ssl-lua/native/cpp",
            "crates/ue4ssl-lua/native/include",
            "crates/ue4ssl-lua-support/vendor/LuaMadeSimple/include",
            "crates/ue4ssl-lua-support/vendor/LuaRaw/include",
            "crates/ue4ssl-cpp-support/vendor/UE4SSL/include",
            "crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_include",
            "crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_src/version.cache",
            "crates/ue4ssl-unreal-support/vendor/Unreal/include",
            "crates/ue4ssl-unreal-support/vendor/Unreal/generated_include",
            "crates/ue4ssl-native-support/vendor/Input/include",
            "crates/ue4ssl-native-support/vendor/Common/include",
            "crates/ue4ssl-native-support/vendor/DynamicOutput/include",
            "crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include",
            "crates/ue4ssl-unreal-support/vendor/Function/include",
            "crates/ue4ssl-hook/include",
        ],
    );
    require_paths_exist("ue4ssl-lua sources", lua_sources(&workspace_root));

    for tracked in [
        workspace_root.join("crates/ue4ssl-lua/native/cpp"),
        workspace_root.join("crates/ue4ssl-lua/native/include"),
        workspace_root.join("crates/ue4ssl-lua-support/vendor/LuaMadeSimple/include"),
        workspace_root.join("crates/ue4ssl-lua-support/vendor/LuaRaw/include"),
        workspace_root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/include"),
        workspace_root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_include"),
        workspace_root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_src"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/generated_include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Input/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Common/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/DynamicOutput/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Function/include"),
        workspace_root.join("crates/ue4ssl-hook/include"),
    ] {
        emit_rerun_for_tree(&tracked);
    }

    let generated = generate_abi_headers(&out_dir).expect("failed to generate ABI headers");
    compile_lua_archive(
        &workspace_root,
        &generated.ue4ssl_include,
        &generated.unreal_include,
        profile,
    );

    let target_dir = target_dir(&workspace_root, profile);
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        target_dir.join("UE4SSL.dll.lib").display()
    );

    let archive = cc_archive_path(&out_dir, "ue4ssl_lua_mod_cpp");
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&archive)
    );
}

fn compile_lua_archive(
    workspace_root: &Path,
    generated_ue4ssl_include: &Path,
    generated_unreal_include: &Path,
    profile: BuildProfile,
) {
    let mut build = Build::new();
    apply_common_msvc_flags(&mut build, profile, true, "/std:c++23preview");
    apply_common_defines(&mut build, profile);
    build.warnings(false);

    for include in [
        workspace_root.join("crates/ue4ssl-lua/native/include"),
        workspace_root.join("crates/ue4ssl-lua/native/cpp"),
        workspace_root.join("crates/ue4ssl-lua-support/vendor/LuaMadeSimple/include"),
        workspace_root.join("crates/ue4ssl-lua-support/vendor/LuaRaw/include"),
        workspace_root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/include"),
        workspace_root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_include"),
        generated_ue4ssl_include.to_path_buf(),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/generated_include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Core"),
        generated_unreal_include.to_path_buf(),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Input/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/Common/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/DynamicOutput/include"),
        workspace_root.join("crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include"),
        workspace_root.join("crates/ue4ssl-unreal-support/vendor/Function/include"),
        workspace_root.join("crates/ue4ssl-hook/include"),
    ] {
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
        "crates/ue4ssl-lua/native/cpp/dllmain.cpp",
        "crates/ue4ssl-lua/native/cpp/LuaCompat.cpp",
        "crates/ue4ssl-lua/native/cpp/LuaEngineRegistry.cpp",
        "crates/ue4ssl-lua/native/cpp/LuaLibrary.cpp",
        "crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp",
    ]
    .into_iter()
    .map(|relative| workspace_root.join(relative))
    .collect();

    let lua_type_dir = workspace_root
        .join("crates/ue4ssl-lua/native/cpp")
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
