#![allow(dead_code)]

use std::ffi::OsStr;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BuildProfile {
    Dev,
    Release,
}

impl BuildProfile {
    pub fn from_env() -> Self {
        match std::env::var("PROFILE").as_deref() {
            Ok("release") => Self::Release,
            _ => Self::Dev,
        }
    }

    pub fn cargo_dir(self) -> &'static str {
        match self {
            Self::Dev => "debug",
            Self::Release => "release",
        }
    }

    pub fn configuration_name(self) -> &'static str {
        match self {
            Self::Dev => "Game__Dev__Win64",
            Self::Release => "Game__Shipping__Win64",
        }
    }

    pub fn is_debug(self) -> bool {
        matches!(self, Self::Dev)
    }
}

pub struct GeneratedAbiIncludeRoots {
    pub ue4ssl_include: PathBuf,
    pub unreal_include: PathBuf,
}

pub const UE4SSL_CPP_SUPPORT_ROOT: &str = "crates/ue4ssl-cpp-support/vendor/UE4SSL";
pub const UE4SSL_UNREAL_SUPPORT_ROOT: &str = "crates/ue4ssl-unreal-support/vendor";
pub const UE4SSL_OBJECT_SEARCHER_INCLUDE_ROOT: &str = "crates/ue4ssl-object-searcher/include";
pub const UE4SSL_NATIVE_COMMON_INCLUDE_ROOT: &str =
    "crates/ue4ssl-native-support/vendor/Common/include";
pub const UE4SS_HOOK_ROOT: &str = "crates/ue4ssl-hook";

pub fn workspace_root_from_manifest_dir(manifest_dir: &Path) -> PathBuf {
    for candidate in manifest_dir.ancestors() {
        if candidate.join("Cargo.toml").is_file() && candidate.join("crates").is_dir() {
            return candidate.to_path_buf();
        }
    }

    panic!(
        "failed to locate workspace root from manifest dir {}",
        manifest_dir.display()
    )
}

pub fn target_dir(workspace_root: &Path, profile: BuildProfile) -> PathBuf {
    if let Some(target_dir) = target_dir_from_out_dir(profile) {
        return target_dir;
    }

    workspace_root.join("target").join(profile.cargo_dir())
}

pub fn ue4ssl_cpp_root(workspace_root: &Path) -> PathBuf {
    workspace_root.join(UE4SSL_CPP_SUPPORT_ROOT)
}

pub fn ue4ssl_unreal_vendor_root(workspace_root: &Path) -> PathBuf {
    workspace_root.join(UE4SSL_UNREAL_SUPPORT_ROOT)
}

pub fn unreal_root(workspace_root: &Path) -> PathBuf {
    ue4ssl_unreal_vendor_root(workspace_root).join("Unreal")
}

fn target_dir_from_out_dir(profile: BuildProfile) -> Option<PathBuf> {
    let mut dir = PathBuf::from(std::env::var_os("OUT_DIR")?);
    loop {
        if dir.file_name() == Some(OsStr::new(profile.cargo_dir())) {
            return Some(dir);
        }

        if !dir.pop() {
            return None;
        }
    }
}

pub fn ue4ss_hook_root(workspace_root: &Path) -> PathBuf {
    workspace_root.join(UE4SS_HOOK_ROOT)
}

pub fn native_support_include_dirs(workspace_root: &Path) -> Vec<PathBuf> {
    [
        "crates/ue4ssl-native-support/vendor/Input/include",
        UE4SSL_NATIVE_COMMON_INCLUDE_ROOT,
        "crates/ue4ssl-native-support/vendor/DynamicOutput/include",
        "crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include",
    ]
    .into_iter()
    .map(|relative| workspace_root.join(relative))
    .collect()
}

pub fn unreal_base_include_dirs(workspace_root: &Path) -> Vec<PathBuf> {
    let unreal_root = unreal_root(workspace_root);
    [
        unreal_root.join("include").join("Function"),
        workspace_root.join(UE4SSL_OBJECT_SEARCHER_INCLUDE_ROOT),
        ue4ss_hook_root(workspace_root).join("include"),
    ]
    .into_iter()
    .collect()
}

pub fn unreal_include_dirs(
    workspace_root: &Path,
    generated_unreal_include: Option<&Path>,
) -> Vec<PathBuf> {
    let unreal_root = unreal_root(workspace_root);
    let mut dirs = vec![
        unreal_root.join("include"),
        unreal_root.join("generated_include"),
        unreal_root.join("include").join("Unreal"),
        unreal_root.join("include").join("Unreal").join("Core"),
    ];
    if let Some(generated_unreal_include) = generated_unreal_include {
        dirs.push(generated_unreal_include.to_path_buf());
    }
    dirs
}

pub fn ue4ssl_include_dirs(
    workspace_root: &Path,
    generated_ue4ssl_include: Option<&Path>,
) -> Vec<PathBuf> {
    let ue4ssl_root = ue4ssl_cpp_root(workspace_root);
    let mut dirs = vec![
        ue4ssl_root.join("include"),
        ue4ssl_root.join("generated_include"),
    ];
    if let Some(generated_ue4ssl_include) = generated_ue4ssl_include {
        dirs.push(generated_ue4ssl_include.to_path_buf());
    }
    dirs
}

pub fn common_native_include_dirs(
    workspace_root: &Path,
    generated_ue4ssl_include: Option<&Path>,
    generated_unreal_include: Option<&Path>,
) -> Vec<PathBuf> {
    let mut dirs = native_support_include_dirs(workspace_root);
    dirs.extend(unreal_base_include_dirs(workspace_root));
    dirs.extend(ue4ssl_include_dirs(
        workspace_root,
        generated_ue4ssl_include,
    ));
    dirs.extend(unreal_include_dirs(
        workspace_root,
        generated_unreal_include,
    ));
    dirs
}

pub fn emit_rerun_for_tree(path: &Path) {
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

pub fn collect_sources(dir: &Path, extensions: &[&str]) -> Vec<PathBuf> {
    if !dir.is_dir() {
        return Vec::new();
    }

    let mut out = Vec::new();
    collect_sources_inner(dir, extensions, &mut out);
    out.sort();
    out
}

pub fn require_paths_exist<I, P>(component: &str, paths: I)
where
    I: IntoIterator<Item = P>,
    P: AsRef<Path>,
{
    let mut missing: Vec<String> = paths
        .into_iter()
        .filter_map(|path| {
            let path = path.as_ref();
            (!path.exists()).then(|| path.display().to_string())
        })
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

pub fn require_relative_paths_exist(component: &str, workspace_root: &Path, paths: &[&str]) {
    require_paths_exist(
        component,
        paths.iter().map(|path| workspace_root.join(path)),
    );
}

pub fn require_nonempty_sources(component: &str, source_root: &Path, sources: &[PathBuf]) {
    if sources.is_empty() {
        panic!(
            "{component} did not find any source files under {}.\n\nRestore the missing source tree/submodule or remove the artifact from the active build baseline.",
            source_root.display()
        );
    }

    require_paths_exist(component, sources);
}

fn collect_sources_inner(dir: &Path, extensions: &[&str], out: &mut Vec<PathBuf>) {
    let mut entries: Vec<PathBuf> = fs::read_dir(dir)
        .unwrap_or_else(|err| panic!("failed to read {}: {err}", dir.display()))
        .map(|entry| {
            entry
                .unwrap_or_else(|err| panic!("failed to read entry in {}: {err}", dir.display()))
                .path()
        })
        .collect();
    entries.sort();

    for entry in entries {
        if entry.is_dir() {
            collect_sources_inner(&entry, extensions, out);
            continue;
        }

        if matches!(
            entry.extension().and_then(OsStr::to_str),
            Some(extension) if extensions.contains(&extension)
        ) {
            out.push(entry);
        }
    }
}

pub fn apply_common_msvc_flags(
    build: &mut cc::Build,
    profile: BuildProfile,
    cpp: bool,
    std_flag: &str,
) {
    build.cpp(cpp);
    build.static_crt(false);
    build.debug(profile.is_debug());
    build.flag("-MD");
    build.flag("/utf-8");
    build.flag("/MP");
    build.flag("/wd4005");
    build.flag("/wd4068");
    build.flag("/wd4251");
    build.flag("/Zc:inline");
    build.flag("/Zc:strictStrings");
    build.flag("/Zc:preprocessor");
    build.flag("/bigobj");

    if cpp {
        build.flag(std_flag);
        build.flag("/EHsc");
    }
}

pub fn apply_common_defines(build: &mut cc::Build, profile: BuildProfile) {
    define(build, "_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR", Some("1"));
    define(build, "UE_GAME", None);
    define(build, "PLATFORM_WINDOWS", None);
    define(build, "PLATFORM_MICROSOFT", None);
    define(build, "OVERRIDE_PLATFORM_HEADER_NAME", Some("Windows"));
    define(build, "UBT_COMPILED_PLATFORM", Some("Win64"));
    define(build, "UNICODE", None);
    define(build, "_UNICODE", None);
    define(build, "WINVER", Some("0x0A00"));
    define(build, "_WIN32_WINNT", Some("0x0A00"));

    if profile.is_debug() {
        define(build, "UE_BUILD_DEVELOPMENT", None);
        define(build, "STATS", None);
    } else {
        define(build, "UE_BUILD_SHIPPING", None);
    }

    define(build, "_ITERATOR_DEBUG_LEVEL", Some("0"));
}

pub fn define(build: &mut cc::Build, name: &str, value: Option<&str>) {
    build.define(name, value);
}

pub fn add_defines(build: &mut cc::Build, defines: &[(&str, Option<&str>)]) {
    for (name, value) in defines {
        define(build, name, *value);
    }
}

pub fn git_short_sha(workspace_root: &Path) -> String {
    let output = Command::new("git")
        .current_dir(workspace_root)
        .args(["rev-parse", "--short", "HEAD"])
        .output();

    match output {
        Ok(output) if output.status.success() => {
            String::from_utf8_lossy(&output.stdout).trim().to_owned()
        }
        _ => "unknown".to_owned(),
    }
}

pub fn version_defines(
    workspace_root: &Path,
    profile: BuildProfile,
) -> Vec<(String, Option<String>)> {
    let version_path = ue4ssl_cpp_root(workspace_root)
        .join("generated_src")
        .join("version.cache");
    let version = fs::read_to_string(&version_path)
        .unwrap_or_else(|err| panic!("failed to read {}: {err}", version_path.display()));

    let mut numbers: Vec<String> = version
        .split(|ch: char| !ch.is_ascii_digit())
        .filter(|segment| !segment.is_empty())
        .map(ToOwned::to_owned)
        .collect();
    numbers.resize(5, "0".to_owned());

    vec![
        (
            "UE4SS_LIB_VERSION_MAJOR".to_owned(),
            Some(numbers[0].clone()),
        ),
        (
            "UE4SS_LIB_VERSION_MINOR".to_owned(),
            Some(numbers[1].clone()),
        ),
        (
            "UE4SS_LIB_VERSION_HOTFIX".to_owned(),
            Some(numbers[2].clone()),
        ),
        (
            "UE4SS_LIB_VERSION_PRERELEASE".to_owned(),
            Some(numbers[3].clone()),
        ),
        (
            "UE4SS_LIB_VERSION_BETA".to_owned(),
            Some(numbers[4].clone()),
        ),
        ("UE4SS_LIB_BETA_STARTED".to_owned(), Some("1".to_owned())),
        ("UE4SS_LIB_IS_BETA".to_owned(), Some("1".to_owned())),
        (
            "UE4SS_LIB_BUILD_GITSHA".to_owned(),
            Some(format!("\"{}\"", git_short_sha(workspace_root))),
        ),
        (
            "UE4SS_CONFIGURATION".to_owned(),
            Some(format!("\"{}\"", profile.configuration_name())),
        ),
    ]
}

pub fn write_if_changed(path: &Path, contents: &str) -> io::Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }

    match fs::read_to_string(path) {
        Ok(existing) if existing == contents => Ok(()),
        _ => fs::write(path, contents),
    }
}

pub fn generate_abi_headers(out_dir: &Path) -> io::Result<GeneratedAbiIncludeRoots> {
    let generated_root = out_dir.join("generated_abi");
    let ue4ssl_include = generated_root
        .join("crates")
        .join("ue4ssl-cpp-support")
        .join("vendor")
        .join("UE4SSL")
        .join("include");
    let unreal_include = generated_root
        .join("crates")
        .join("ue4ssl-unreal-support")
        .join("vendor")
        .join("Unreal")
        .join("include");

    write_if_changed(
        &ue4ssl_include
            .join("Compat")
            .join("GeneratedRustCoreAbi.hpp"),
        &ue4ssl_abi::render_rustcore_header(),
    )?;
    write_if_changed(
        &ue4ssl_include.join("Compat").join("GeneratedHostAbi.hpp"),
        &ue4ssl_abi::render_host_header(),
    )?;
    write_if_changed(
        &unreal_include
            .join("Unreal")
            .join("Compat")
            .join("GeneratedPsScanAbi.hpp"),
        &ue4ssl_abi::render_scan_header(),
    )?;

    Ok(GeneratedAbiIncludeRoots {
        ue4ssl_include,
        unreal_include,
    })
}

pub fn cc_archive_path(out_dir: &Path, archive_stem: &str) -> PathBuf {
    out_dir.join(format!("{archive_stem}.lib"))
}

pub fn emit_static_archive_metadata(key: &str, path: &Path) {
    println!("cargo:metadata={key}={}", path.display());
    println!("cargo:{key}={}", path.display());
}

pub fn support_archive_from_env(links: &str, key: &str) -> PathBuf {
    let links = links
        .chars()
        .map(|ch| {
            if ch.is_ascii_alphanumeric() {
                ch.to_ascii_uppercase()
            } else {
                '_'
            }
        })
        .collect::<String>();
    let key = key
        .chars()
        .map(|ch| {
            if ch.is_ascii_alphanumeric() {
                ch.to_ascii_uppercase()
            } else {
                '_'
            }
        })
        .collect::<String>();
    let env_key = format!("DEP_{links}_{key}");
    PathBuf::from(
        std::env::var_os(&env_key)
            .unwrap_or_else(|| panic!("missing {env_key} from support crate")),
    )
}

pub fn whole_archive_flag(path: &Path) -> String {
    format!("/WHOLEARCHIVE:{}", path.display())
}

pub fn emit_dylib_link(name: &str) {
    println!("cargo:rustc-link-lib=dylib={name}");
}
