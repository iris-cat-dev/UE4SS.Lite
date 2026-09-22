pub mod common;

use std::collections::{BTreeMap, BTreeSet};
use std::env;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use cc::Build;
use common::{
    apply_common_defines, apply_common_msvc_flags, cc_archive_path, common_native_include_dirs,
    define, emit_dylib_link, emit_rerun_for_tree, generate_abi_headers, require_paths_exist,
    target_dir, version_defines, whole_archive_flag, workspace_root_from_manifest_dir,
    BuildProfile,
};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
struct BuildInput {
    mod_name: String,
    workspace_root: String,
    source_files: Vec<String>,
    include_dirs: Vec<String>,
    link_libraries: Vec<String>,
    defines: BTreeMap<String, Option<String>>,
    compiler_flags: Vec<String>,
    cpp_standard: String,
    archive_stem: String,
}

pub fn build_from_file(path: impl AsRef<Path>) {
    if let Err(error) = try_build_from_file(path.as_ref()) {
        panic!("ue4ssl native mod build failed: {error:#}");
    }
}

fn try_build_from_file(path: &Path) -> Result<()> {
    println!("cargo:rerun-if-changed={}", path.display());

    let text = std::fs::read_to_string(path)
        .with_context(|| format!("failed to read build input {}", path.display()))?;
    let input: BuildInput = serde_json::from_str(&text)
        .with_context(|| format!("failed to parse build input {}", path.display()))?;

    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        bail!("native C++ mods currently support only windows-msvc targets");
    }

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").context("missing CARGO_MANIFEST_DIR")?);
    let workspace_root = PathBuf::from(&input.workspace_root);
    let derived_workspace_root = workspace_root_from_manifest_dir(&manifest_dir);
    if derived_workspace_root != workspace_root {
        println!(
            "cargo:warning=generated mod package lives outside the source workspace; using configured workspace root {}",
            workspace_root.display()
        );
    }

    let out_dir = PathBuf::from(env::var_os("OUT_DIR").context("missing OUT_DIR")?);
    let profile = BuildProfile::from_env();
    let source_files = input
        .source_files
        .iter()
        .map(PathBuf::from)
        .collect::<Vec<_>>();
    if source_files.is_empty() {
        bail!("native mod {} has no C++ sources", input.mod_name);
    }
    let source_label = format!("{} sources", input.mod_name);
    require_paths_exist(&source_label, source_files.iter());

    let include_dirs = input
        .include_dirs
        .iter()
        .map(PathBuf::from)
        .collect::<Vec<_>>();
    let include_label = format!("{} include dirs", input.mod_name);
    require_paths_exist(&include_label, include_dirs.iter());

    println!(
        "cargo:rerun-if-changed={}",
        workspace_root.join("version.cache").display()
    );
    for source in &source_files {
        println!("cargo:rerun-if-changed={}", source.display());
    }
    for include_dir in &include_dirs {
        emit_rerun_for_tree(include_dir);
    }
    emit_rerun_for_tree(&workspace_root.join("crates/ue4ssl-dll/native/UE4SSL/include"));
    emit_rerun_for_tree(&workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include"));
    emit_rerun_for_tree(&workspace_root.join("crates/ue4ssl-dll/native/UE4SSL/generated_include"));
    emit_rerun_for_tree(
        &workspace_root.join("crates/ue4ssl-unreal-support/vendor/Unreal/generated_include"),
    );

    let generated = generate_abi_headers(&out_dir).context("failed to generate ABI headers")?;
    compile_archive(
        &input,
        &workspace_root,
        &source_files,
        &include_dirs,
        &generated.ue4ssl_include,
        &generated.unreal_include,
        profile,
    );

    emit_link_libraries(&input);

    let import_lib = target_dir(&workspace_root, profile).join("UE4SSL.dll.lib");
    if !import_lib.exists() {
        bail!(
            "expected UE4SSL import library at {}. Build the core artifact before native mods.",
            import_lib.display()
        );
    }
    println!("cargo:rustc-link-arg-cdylib={}", import_lib.display());

    let archive = cc_archive_path(&out_dir, &input.archive_stem);
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&archive)
    );

    Ok(())
}

fn compile_archive(
    input: &BuildInput,
    workspace_root: &Path,
    source_files: &[PathBuf],
    include_dirs: &[PathBuf],
    generated_ue4ssl_include: &Path,
    generated_unreal_include: &Path,
    profile: BuildProfile,
) {
    let mut build = Build::new();
    build.cargo_metadata(false);
    apply_common_msvc_flags(&mut build, profile, true, &input.cpp_standard);
    apply_common_defines(&mut build, profile);
    build.warnings(false);
    build.flag("/wd4996");

    for include in common_native_include_dirs(
        workspace_root,
        Some(generated_ue4ssl_include),
        Some(generated_unreal_include),
    ) {
        build.include(include);
    }
    for include in include_dirs {
        build.include(include);
    }

    for (name, value) in version_defines(workspace_root, profile) {
        define(&mut build, &name, value.as_deref());
    }
    for (name, value) in &input.defines {
        build.define(name, value.as_deref());
    }
    for flag in &input.compiler_flags {
        build.flag(flag);
    }
    for source in source_files {
        build.file(source);
    }

    build.compile(&input.archive_stem);
}

fn emit_link_libraries(input: &BuildInput) {
    let mut libraries = BTreeSet::new();
    libraries.insert("kernel32".to_owned());
    libraries.insert("user32".to_owned());
    libraries.extend(input.link_libraries.iter().cloned());

    for library in libraries {
        emit_dylib_link(&library);
    }
}
