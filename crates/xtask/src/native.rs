#![allow(dead_code)]

use std::collections::{BTreeMap, HashMap, HashSet};
use std::fs;

use anyhow::{bail, Context, Result};
use camino::{Utf8Path, Utf8PathBuf};
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CargoProfile {
    Dev,
    Release,
}

impl CargoProfile {
    pub fn cargo_dir(self) -> &'static str {
        match self {
            Self::Dev => "debug",
            Self::Release => "release",
        }
    }
}

fn explicit_target(target: Option<&str>) -> Option<&str> {
    target.and_then(|value| {
        let value = value.trim();
        (!value.is_empty()).then_some(value)
    })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NativeStrategy {
    BuildRsCc,
    BuildRsToolchain,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PackageKind {
    CoreDll,
    ScriptEngine,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct NativeGroup {
    pub name: &'static str,
    pub strategy: NativeStrategy,
    pub legacy_targets: &'static [&'static str],
    pub source_roots: &'static [&'static str],
    pub include_roots: &'static [&'static str],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ArtifactSpec {
    pub package_name: &'static str,
    pub cargo_target_stem: &'static str,
    pub binary_name: &'static str,
    pub kind: PackageKind,
    pub mod_directory_name: Option<&'static str>,
    pub extra_stage_roots: &'static [&'static str],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NativeModSpec {
    pub mod_name: String,
    pub mod_root: Utf8PathBuf,
    pub package_name: String,
    pub cargo_target_stem: String,
    pub source_files: Vec<Utf8PathBuf>,
    pub include_dirs: Vec<Utf8PathBuf>,
    pub resource_roots: Vec<Utf8PathBuf>,
    pub link_libraries: Vec<String>,
    pub defines: BTreeMap<String, Option<String>>,
    pub compiler_flags: Vec<String>,
    pub cpp_standard: String,
}

#[derive(Clone, Debug, Serialize)]
pub struct NativeModBuildInput {
    pub mod_name: String,
    pub workspace_root: String,
    pub source_files: Vec<String>,
    pub include_dirs: Vec<String>,
    pub link_libraries: Vec<String>,
    pub defines: BTreeMap<String, Option<String>>,
    pub compiler_flags: Vec<String>,
    pub cpp_standard: String,
    pub archive_stem: String,
}

#[derive(Debug, Default, Deserialize)]
#[serde(default, deny_unknown_fields)]
struct NativeModManifest {
    source_dirs: Vec<String>,
    sources: Vec<String>,
    include_dirs: Vec<String>,
    resources: Vec<String>,
    link_libraries: Vec<String>,
    defines: BTreeMap<String, Option<String>>,
    compiler_flags: Vec<String>,
    cpp_standard: Option<String>,
}

pub const NATIVE_GROUPS: &[NativeGroup] = &[
    NativeGroup {
        name: "ue4ssl_platform",
        strategy: NativeStrategy::BuildRsCc,
        legacy_targets: &["ue4ssl_native_support", "Input", "Helpers", "DynamicOutput"],
        source_roots: &[
            "crates/ue4ssl-platform/native/Input/src",
            "crates/ue4ssl-platform/native/DynamicOutput/src",
        ],
        include_roots: &[
            "crates/ue4ssl-platform/native/Input/include",
            "crates/ue4ssl-platform/native/Common/include",
            "crates/ue4ssl-platform/native/DynamicOutput/include",
        ],
    },
    NativeGroup {
        name: "ue4ssl_core",
        strategy: NativeStrategy::BuildRsCc,
        legacy_targets: &["UE4SSL", "MProgram"],
        source_roots: &["crates/ue4ssl-dll/native/UE4SSL/src"],
        include_roots: &[
            "crates/ue4ssl-dll/native/UE4SSL/include",
            "crates/ue4ssl-dll/native/UE4SSL/generated_include",
        ],
    },
    NativeGroup {
        name: "patternsleuth_bind",
        strategy: NativeStrategy::BuildRsCc,
        legacy_targets: &["SinglePassSigScanner"],
        source_roots: &["crates/patternsleuth-bind/native/SinglePassSigScanner/src"],
        include_roots: &["crates/patternsleuth-bind/native/SinglePassSigScanner/include"],
    },
    NativeGroup {
        name: "ue4ssl_unreal_support",
        strategy: NativeStrategy::BuildRsCc,
        legacy_targets: &["Constructs", "Function", "Unreal"],
        source_roots: &["crates/ue4ssl-unreal-support/vendor/Unreal/src"],
        include_roots: &[
            "crates/ue4ssl-platform/native/Common/include",
            "crates/ue4ssl-unreal-support/vendor/Unreal/include/Function",
            "crates/ue4ssl-unreal-support/vendor/Unreal/include",
            "crates/ue4ssl-unreal-support/vendor/Unreal/generated_include",
        ],
    },
    NativeGroup {
        name: "ue4ssl_javascript_native",
        strategy: NativeStrategy::BuildRsToolchain,
        legacy_targets: &["UE4SSL.JavaScript"],
        source_roots: &["crates/ue4ssl-javascript/native/cpp"],
        include_roots: &[
            "crates/ue4ssl-javascript/native/include",
            "crates/ue4ssl-javascript-support/vendor/quickjs",
        ],
    },
    NativeGroup {
        name: "ue4ssl_javascript_support",
        strategy: NativeStrategy::BuildRsCc,
        legacy_targets: &["ue4ssl_javascript_support"],
        source_roots: &["crates/ue4ssl-javascript-support/vendor/quickjs"],
        include_roots: &["crates/ue4ssl-javascript-support/vendor/quickjs"],
    },
    NativeGroup {
        name: "ue4ssl_lua_native",
        strategy: NativeStrategy::BuildRsToolchain,
        legacy_targets: &["UE4SSL.Lua"],
        source_roots: &["crates/ue4ssl-lua/native/cpp"],
        include_roots: &[
            "crates/ue4ssl-lua/native/include",
            "crates/ue4ssl-lua-support/vendor/LuaMadeSimple/include",
            "crates/ue4ssl-lua-support/vendor/LuaRaw/include",
        ],
    },
    NativeGroup {
        name: "ue4ssl_lua_support",
        strategy: NativeStrategy::BuildRsCc,
        legacy_targets: &["ue4ssl_lua_support", "LuaRaw", "LuaMadeSimple"],
        source_roots: &[
            "crates/ue4ssl-lua-support/vendor/LuaRaw/src",
            "crates/ue4ssl-lua-support/vendor/LuaMadeSimple/src",
        ],
        include_roots: &[
            "crates/ue4ssl-lua-support/vendor/LuaRaw/include",
            "crates/ue4ssl-lua-support/vendor/LuaMadeSimple/include",
        ],
    },
    NativeGroup {
        name: "ue4ssl_paksync_native",

        strategy: NativeStrategy::BuildRsToolchain,

        legacy_targets: &["UE4SSL.PakSync"],

        source_roots: &["crates/ue4ssl-paksync/native/cpp"],

        include_roots: &["crates/ue4ssl-paksync/native/include"],
    },
    NativeGroup {
        name: "ue4ssl_proxy_native",
        strategy: NativeStrategy::BuildRsToolchain,
        legacy_targets: &["proxy"],
        source_roots: &["crates/ue4ssl-proxy"],
        include_roots: &[],
    },
];

pub const CORE_ARTIFACTS: &[ArtifactSpec] = &[ArtifactSpec {
    package_name: "ue4ssl-dll",
    cargo_target_stem: "UE4SSL",
    binary_name: "UE4SSL",
    kind: PackageKind::CoreDll,
    mod_directory_name: None,
    extra_stage_roots: &[],
}];

pub const SCRIPT_ENGINE_ARTIFACTS: &[ArtifactSpec] = &[
    ArtifactSpec {
        package_name: "ue4ssl-javascript",
        cargo_target_stem: "ue4ssl_javascript",
        binary_name: "UE4SSL.JavaScript",
        kind: PackageKind::ScriptEngine,
        mod_directory_name: Some("UE4SSL.JavaScript"),
        extra_stage_roots: &[],
    },
    ArtifactSpec {
        package_name: "ue4ssl-lua",
        cargo_target_stem: "ue4ssl_lua",
        binary_name: "UE4SSL.Lua",
        kind: PackageKind::ScriptEngine,
        mod_directory_name: Some("UE4SSL.Lua"),
        extra_stage_roots: &[],
    },
];

pub const MOD_ARTIFACTS: &[ArtifactSpec] = &[ArtifactSpec {
    package_name: "ue4ssl-paksync",

    cargo_target_stem: "ue4ssl_paksync",

    binary_name: "UE4SSL.PakSync",

    kind: PackageKind::Mod,

    mod_directory_name: Some("UE4SSL.PakSync"),

    extra_stage_roots: &["crates/ue4ssl-paksync/config"],
}];

pub fn artifact_by_package(package_name: &str) -> Option<&'static ArtifactSpec> {
    default_artifacts().find(|artifact| artifact.package_name == package_name)
}

pub fn default_artifacts() -> impl Iterator<Item = &'static ArtifactSpec> {
    CORE_ARTIFACTS
        .iter()
        .chain(SCRIPT_ENGINE_ARTIFACTS.iter())
        .chain(MOD_ARTIFACTS.iter())
}

pub fn core_artifacts() -> impl Iterator<Item = &'static ArtifactSpec> {
    CORE_ARTIFACTS.iter()
}

pub fn runtime_artifacts() -> impl Iterator<Item = &'static ArtifactSpec> {
    SCRIPT_ENGINE_ARTIFACTS.iter().chain(MOD_ARTIFACTS.iter())
}

pub fn discover_native_mods(workspace_root: &Utf8Path) -> Result<Vec<NativeModSpec>> {
    let mods_root = workspace_root.join("Mods");
    if !mods_root.is_dir() {
        return Ok(Vec::new());
    }

    let mut entries = fs::read_dir(mods_root.as_std_path())
        .with_context(|| format!("failed to read {}", mods_root))?
        .collect::<std::io::Result<Vec<_>>>()
        .with_context(|| format!("failed to enumerate {}", mods_root))?;
    entries.sort_by_key(|entry| entry.file_name());

    let mut specs = Vec::new();
    for entry in entries {
        if !entry
            .file_type()
            .with_context(|| format!("failed to stat {:?}", entry.path()))?
            .is_dir()
        {
            continue;
        }

        let mod_root = Utf8PathBuf::from_path_buf(entry.path())
            .map_err(|path| anyhow::anyhow!("non-UTF8 mod path {}", path.display()))?;
        let mod_name = mod_root
            .file_name()
            .context("mod directory is missing a name")?
            .to_owned();
        if mod_name.eq_ignore_ascii_case("shared") {
            continue;
        }

        let default_source_dir = mod_root.join("native").join("cpp");
        if !default_source_dir.is_dir() {
            continue;
        }

        let manifest = read_mod_manifest(&mod_root)?;
        let mut source_dirs = if manifest.source_dirs.is_empty() {
            vec![default_source_dir]
        } else {
            manifest
                .source_dirs
                .iter()
                .map(|path| mod_relative_path(&mod_root, path))
                .collect::<Result<Vec<_>>>()?
        };
        source_dirs.sort();
        source_dirs.dedup();

        let mut source_files = Vec::new();
        for source_dir in &source_dirs {
            if !source_dir.is_dir() {
                bail!("native mod {mod_name} source dir does not exist: {source_dir}");
            }
            collect_cpp_sources(source_dir, &mut source_files)?;
        }
        for source in &manifest.sources {
            let source = mod_relative_path(&mod_root, source)?;
            if !source.is_file() {
                bail!("native mod {mod_name} source file does not exist: {source}");
            }
            if !is_cpp_source(&source) {
                bail!("native mod {mod_name} source file is not C++: {source}");
            }
            source_files.push(source);
        }
        source_files.sort();
        source_files.dedup();
        if source_files.is_empty() {
            bail!("native mod {mod_name} has no C++ sources under native/cpp");
        }

        let mut include_dirs = Vec::new();
        for default_include in [
            mod_root.join("native").join("include"),
            mod_root.join("include"),
        ] {
            if default_include.is_dir() {
                include_dirs.push(default_include);
            }
        }
        for include_dir in &manifest.include_dirs {
            let include_dir = mod_relative_path(&mod_root, include_dir)?;
            if !include_dir.is_dir() {
                bail!("native mod {mod_name} include dir does not exist: {include_dir}");
            }
            include_dirs.push(include_dir);
        }
        include_dirs.sort();
        include_dirs.dedup();

        let mut resource_roots = Vec::new();
        let default_resources = mod_root.join("resources");
        if default_resources.is_dir() {
            resource_roots.push(default_resources);
        }
        for resource in &manifest.resources {
            let resource = mod_relative_path(&mod_root, resource)?;
            if !resource.exists() {
                bail!("native mod {mod_name} resource path does not exist: {resource}");
            }
            resource_roots.push(resource);
        }
        resource_roots.sort();
        resource_roots.dedup();

        specs.push(NativeModSpec {
            package_name: format!("ue4ssl-auto-mod-{}", sanitize_for_package(&mod_name)),
            cargo_target_stem: format!("ue4ssl_mod_{}", sanitize_for_library(&mod_name)),
            mod_name,
            mod_root,
            source_files,
            include_dirs,
            resource_roots,
            link_libraries: manifest.link_libraries,
            defines: manifest.defines,
            compiler_flags: manifest.compiler_flags,
            cpp_standard: manifest
                .cpp_standard
                .unwrap_or_else(|| "/std:c++23preview".to_owned()),
        });
    }

    validate_unique_mods(&specs)?;
    Ok(specs)
}

pub fn select_native_mods<'a>(
    mods: &'a [NativeModSpec],
    requested_names: &[String],
) -> Result<Vec<&'a NativeModSpec>> {
    if requested_names.is_empty() {
        return Ok(mods.iter().collect());
    }

    let mut by_lower_name = HashMap::new();
    for spec in mods {
        by_lower_name.insert(spec.mod_name.to_lowercase(), spec);
    }

    let mut selected = Vec::new();
    let mut seen = HashSet::new();
    for requested in requested_names {
        let key = requested.to_lowercase();
        if !seen.insert(key.clone()) {
            continue;
        }
        let Some(spec) = by_lower_name.get(&key) else {
            let available = mods
                .iter()
                .map(|spec| spec.mod_name.as_str())
                .collect::<Vec<_>>()
                .join(", ");
            bail!("unknown native mod '{requested}'. Available native mods: {available}");
        };
        selected.push(*spec);
    }

    Ok(selected)
}

pub fn write_generated_mod_workspace(
    workspace_root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
    mods: &[&NativeModSpec],
) -> Result<Utf8PathBuf> {
    let workspace_dir = generated_mod_workspace_dir(workspace_root, profile, target);
    if workspace_dir.exists() {
        fs::remove_dir_all(workspace_dir.as_std_path())
            .with_context(|| format!("failed to remove generated mod workspace {workspace_dir}"))?;
    }
    fs::create_dir_all(workspace_dir.as_std_path())
        .with_context(|| format!("failed to create generated mod workspace {workspace_dir}"))?;

    let packages_dir = workspace_dir.join("packages");
    fs::create_dir_all(packages_dir.as_std_path())
        .with_context(|| format!("failed to create {packages_dir}"))?;

    let mut members = Vec::new();
    for spec in mods {
        let package_dir = packages_dir.join(&spec.package_name);
        let src_dir = package_dir.join("src");
        fs::create_dir_all(src_dir.as_std_path())
            .with_context(|| format!("failed to create {src_dir}"))?;

        let build_crate = workspace_root.join("crates").join("ue4ssl-build");
        let cargo_toml = format!(
            "[package]\nname = {}\nedition = \"2021\"\nversion = \"0.1.0\"\npublish = false\n\n[lib]\nname = {}\npath = \"src/lib.rs\"\ncrate-type = [\"cdylib\"]\n\n[build-dependencies]\nue4ssl-build = {{ path = {} }}\n",
            toml_string(&spec.package_name),
            toml_string(&spec.cargo_target_stem),
            toml_string(build_crate.as_str())
        );
        write_if_changed(&package_dir.join("Cargo.toml"), &cargo_toml)?;
        write_if_changed(
            &package_dir.join("build.rs"),
            "fn main() {\n    let manifest_dir = std::path::PathBuf::from(std::env::var_os(\"CARGO_MANIFEST_DIR\").expect(\"missing CARGO_MANIFEST_DIR\"));\n    ue4ssl_build::build_from_file(manifest_dir.join(\"ue4ssl-build.json\"));\n}\n",
        )?;
        write_if_changed(
            &src_dir.join("lib.rs"),
            "#![allow(dead_code)]\n\npub fn ue4ssl_generated_mod_anchor() {}\n",
        )?;

        let input = NativeModBuildInput {
            mod_name: spec.mod_name.clone(),
            workspace_root: workspace_root.to_string(),
            source_files: spec.source_files.iter().map(ToString::to_string).collect(),
            include_dirs: spec.include_dirs.iter().map(ToString::to_string).collect(),
            link_libraries: spec.link_libraries.clone(),
            defines: spec.defines.clone(),
            compiler_flags: spec.compiler_flags.clone(),
            cpp_standard: spec.cpp_standard.clone(),
            archive_stem: format!("{}_cpp", spec.cargo_target_stem),
        };
        let input =
            serde_json::to_string_pretty(&input).context("failed to encode mod build input")?;
        write_if_changed(&package_dir.join("ue4ssl-build.json"), &(input + "\n"))?;
        members.push(format!("packages/{}", spec.package_name));
    }

    let members = members
        .iter()
        .map(|member| format!("    {},", toml_string(member)))
        .collect::<Vec<_>>()
        .join("\n");
    let workspace_toml = format!(
        "[workspace]\nresolver = \"2\"\nmembers = [\n{members}\n]\n\n[profile.dev]\ndebug = true\nincremental = true\n\n[profile.release]\ndebug = true\nincremental = true\nlto = \"thin\"\ncodegen-units = 1\n"
    );
    write_if_changed(&workspace_dir.join("Cargo.toml"), &workspace_toml)?;

    Ok(workspace_dir)
}

pub fn built_native_mod_binary_path(
    workspace_root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
    spec: &NativeModSpec,
) -> Utf8PathBuf {
    artifact_binary_path(workspace_root, profile, target, &spec.cargo_target_stem)
}

pub fn built_native_mod_pdb_path(
    workspace_root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
    spec: &NativeModSpec,
) -> Utf8PathBuf {
    artifact_pdb_path(workspace_root, profile, target, &spec.cargo_target_stem)
}

pub fn generated_mod_workspace_dir(
    workspace_root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
) -> Utf8PathBuf {
    workspace_root
        .join("target")
        .join("ue4ssl-generated-mods")
        .join(explicit_target(target).unwrap_or("host"))
        .join(profile.cargo_dir())
}

pub fn cargo_dll_name(target_stem: &str) -> String {
    format!("{target_stem}.dll")
}

pub fn cargo_target_dir(
    workspace_root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
) -> Utf8PathBuf {
    let mut dir = workspace_root.join("target");
    if let Some(target) = explicit_target(target) {
        dir = dir.join(target);
    }
    dir.join(profile.cargo_dir())
}

pub fn artifact_binary_path(
    workspace_root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
    target_stem: &str,
) -> Utf8PathBuf {
    cargo_target_dir(workspace_root, profile, target).join(cargo_dll_name(target_stem))
}

pub fn artifact_pdb_path(
    workspace_root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
    target_stem: &str,
) -> Utf8PathBuf {
    cargo_target_dir(workspace_root, profile, target).join(format!("{target_stem}.pdb"))
}

pub fn artifact_import_lib_path(
    workspace_root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
    binary_name: &str,
) -> Utf8PathBuf {
    cargo_target_dir(workspace_root, profile, target).join(format!("{binary_name}.dll.lib"))
}

pub fn package_profile_dir(
    workspace_root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
) -> Utf8PathBuf {
    let mut dir = workspace_root.join("target").join("package");
    if let Some(target) = explicit_target(target) {
        dir = dir.join(target);
    }
    dir.join(profile.cargo_dir())
}

pub fn package_stage_dir(
    workspace_root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
) -> Utf8PathBuf {
    package_profile_dir(workspace_root, profile, target).join("ue4ss")
}

fn read_mod_manifest(mod_root: &Utf8Path) -> Result<NativeModManifest> {
    let manifest_path = mod_root.join("mod.json");
    if !manifest_path.exists() {
        return Ok(NativeModManifest::default());
    }

    let text = fs::read_to_string(manifest_path.as_std_path())
        .with_context(|| format!("failed to read {manifest_path}"))?;
    serde_json::from_str(&text).with_context(|| format!("failed to parse {manifest_path}"))
}

fn mod_relative_path(mod_root: &Utf8Path, relative: &str) -> Result<Utf8PathBuf> {
    let path = Utf8Path::new(relative);
    if path.is_absolute() {
        bail!("mod paths must be relative, got {relative}");
    }
    Ok(mod_root.join(path))
}

fn collect_cpp_sources(dir: &Utf8Path, out: &mut Vec<Utf8PathBuf>) -> Result<()> {
    let mut entries = fs::read_dir(dir.as_std_path())
        .with_context(|| format!("failed to read {dir}"))?
        .collect::<std::io::Result<Vec<_>>>()
        .with_context(|| format!("failed to enumerate {dir}"))?;
    entries.sort_by_key(|entry| entry.file_name());

    for entry in entries {
        let path = Utf8PathBuf::from_path_buf(entry.path())
            .map_err(|path| anyhow::anyhow!("non-UTF8 source path {}", path.display()))?;
        let file_type = entry
            .file_type()
            .with_context(|| format!("failed to stat {path}"))?;
        if file_type.is_dir() {
            collect_cpp_sources(&path, out)?;
        } else if file_type.is_file() && is_cpp_source(&path) {
            out.push(path);
        }
    }

    Ok(())
}

fn is_cpp_source(path: &Utf8Path) -> bool {
    matches!(
        path.extension().map(str::to_ascii_lowercase).as_deref(),
        Some("cpp" | "cc" | "cxx")
    )
}

fn validate_unique_mods(specs: &[NativeModSpec]) -> Result<()> {
    let mut mod_names = HashMap::new();
    let mut package_names = HashMap::new();
    let mut target_stems = HashMap::new();

    for spec in specs {
        insert_unique(
            &mut mod_names,
            spec.mod_name.to_lowercase(),
            &spec.mod_name,
            "mod name",
        )?;
        insert_unique(
            &mut package_names,
            spec.package_name.clone(),
            &spec.mod_name,
            "generated package name",
        )?;
        insert_unique(
            &mut target_stems,
            spec.cargo_target_stem.clone(),
            &spec.mod_name,
            "generated DLL target",
        )?;
    }

    Ok(())
}

fn insert_unique(
    seen: &mut HashMap<String, String>,
    key: String,
    mod_name: &str,
    label: &str,
) -> Result<()> {
    if let Some(existing) = seen.insert(key.clone(), mod_name.to_owned()) {
        bail!("native mods '{existing}' and '{mod_name}' collide on {label} '{key}'");
    }
    Ok(())
}

fn sanitize_for_package(value: &str) -> String {
    sanitize(value, '-')
}

fn sanitize_for_library(value: &str) -> String {
    sanitize(value, '_')
}

fn sanitize(value: &str, separator: char) -> String {
    let mut out = String::new();
    let mut last_was_separator = true;
    for ch in value.chars() {
        if ch.is_ascii_alphanumeric() {
            out.push(ch.to_ascii_lowercase());
            last_was_separator = false;
        } else if !last_was_separator {
            out.push(separator);
            last_was_separator = true;
        }
    }

    while out.ends_with(separator) {
        out.pop();
    }
    if out.is_empty() {
        out.push_str("mod");
    }
    out
}

fn toml_string(value: &str) -> String {
    serde_json::to_string(value).expect("failed to encode TOML string")
}

fn write_if_changed(path: &Utf8Path, content: &str) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent.as_std_path())
            .with_context(|| format!("failed to create {parent}"))?;
    }

    match fs::read_to_string(path.as_std_path()) {
        Ok(existing) if existing == content => Ok(()),
        _ => {
            fs::write(path.as_std_path(), content)
                .with_context(|| format!("failed to write {path}"))?;
            Ok(())
        }
    }
}
