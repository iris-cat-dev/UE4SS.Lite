use std::{env, fs};

use anyhow::{bail, Context, Result};
use camino::{Utf8Path, Utf8PathBuf};
use clap::{Parser, Subcommand, ValueEnum};
use native::{
    artifact_binary_path, artifact_import_lib_path, artifact_pdb_path, built_native_mod_binary_path,
    built_native_mod_pdb_path, core_artifacts, default_artifacts, discover_native_mods,
    package_profile_dir, package_stage_dir, runtime_artifacts, select_native_mods,
    write_generated_mod_workspace, ArtifactSpec, CargoProfile, NativeModSpec, PackageKind,
};
use xshell::{cmd, Shell};

mod native;

#[derive(Parser)]
#[command(author, version, about = "UE4SSL Cargo orchestration")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    Build {
        #[arg(long, value_enum, default_value_t = ProfileArg::Dev)]
        profile: ProfileArg,
        #[arg(long)]
        target: Option<String>,
        #[arg(long)]
        core_only: bool,
        #[arg(long)]
        mods_only: bool,
        #[arg(long = "mod", value_name = "NAME")]
        mod_names: Vec<String>,
    },
    BuildNativeSupport {
        #[arg(long, value_enum, default_value_t = ProfileArg::Dev)]
        profile: ProfileArg,
        #[arg(long)]
        target: Option<String>,
    },
    BuildProxy {
        #[arg(long, value_enum, default_value_t = ProfileArg::Dev)]
        profile: ProfileArg,
        #[arg(long)]
        target: Option<String>,
        #[arg(long)]
        proxy_path: Option<Utf8PathBuf>,
    },
    Package {
        #[arg(long, value_enum, default_value_t = ProfileArg::Dev)]
        profile: ProfileArg,
        #[arg(long)]
        target: Option<String>,
        #[arg(long)]
        core_only: bool,
        #[arg(long)]
        mods_only: bool,
        #[arg(long = "mod", value_name = "NAME")]
        mod_names: Vec<String>,
        #[arg(long)]
        no_build: bool,
    },
    PackageProxy {
        #[arg(long, value_enum, default_value_t = ProfileArg::Dev)]
        profile: ProfileArg,
        #[arg(long)]
        target: Option<String>,
        #[arg(long)]
        no_build: bool,
        #[arg(long)]
        proxy_path: Option<Utf8PathBuf>,
    },
    Install {
        #[arg(long, value_enum, default_value_t = ProfileArg::Dev)]
        profile: ProfileArg,
        #[arg(long)]
        target: Option<String>,
        #[arg(long)]
        core_only: bool,
        #[arg(long)]
        mods_only: bool,
        #[arg(long = "mod", value_name = "NAME")]
        mod_names: Vec<String>,
        #[arg(long)]
        no_build: bool,
        #[arg(long)]
        destination: Utf8PathBuf,
    },
    InstallProxy {
        #[arg(long, value_enum, default_value_t = ProfileArg::Dev)]
        profile: ProfileArg,
        #[arg(long)]
        target: Option<String>,
        #[arg(long)]
        no_build: bool,
        #[arg(long)]
        proxy_path: Option<Utf8PathBuf>,
        #[arg(long)]
        destination: Utf8PathBuf,
    },
    Mods {
        #[command(subcommand)]
        command: ModsCommand,
    },
    SyncAbi,
}

#[derive(Subcommand)]
enum ModsCommand {
    List,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, ValueEnum)]
enum ProfileArg {
    Dev,
    Release,
}

impl From<ProfileArg> for CargoProfile {
    fn from(value: ProfileArg) -> Self {
        match value {
            ProfileArg::Dev => CargoProfile::Dev,
            ProfileArg::Release => CargoProfile::Release,
        }
    }
}

#[derive(Clone, Debug)]
struct BuildScope {
    core_only: bool,
    mods_only: bool,
    mod_names: Vec<String>,
}

impl BuildScope {
    fn new(core_only: bool, mods_only: bool, mod_names: Vec<String>) -> Result<Self> {
        if core_only && (mods_only || !mod_names.is_empty()) {
            bail!("--core-only cannot be combined with --mods-only or --mod");
        }
        Ok(Self {
            core_only,
            mods_only,
            mod_names,
        })
    }

    fn only_mods(&self) -> bool {
        self.mods_only || !self.mod_names.is_empty()
    }
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.command {
        Command::Build {
            profile,
            target,
            core_only,
            mods_only,
            mod_names,
        } => build(
            profile.into(),
            target.as_deref(),
            BuildScope::new(core_only, mods_only, mod_names)?,
        ),
        Command::BuildNativeSupport { profile, target } => {
            build_native_support(profile.into(), target.as_deref())
        }
        Command::BuildProxy {
            profile,
            target,
            proxy_path,
        } => build_proxy(profile.into(), target.as_deref(), proxy_path),
        Command::Package {
            profile,
            target,
            core_only,
            mods_only,
            mod_names,
            no_build,
        } => {
            package(
                profile.into(),
                target.as_deref(),
                no_build,
                BuildScope::new(core_only, mods_only, mod_names)?,
            )?;
            Ok(())
        }
        Command::PackageProxy {
            profile,
            target,
            no_build,
            proxy_path,
        } => {
            package_proxy(profile.into(), target.as_deref(), no_build, proxy_path)?;
            Ok(())
        }
        Command::Install {
            profile,
            target,
            core_only,
            mods_only,
            mod_names,
            no_build,
            destination,
        } => install(
            profile.into(),
            target.as_deref(),
            no_build,
            BuildScope::new(core_only, mods_only, mod_names)?,
            destination,
        ),
        Command::InstallProxy {
            profile,
            target,
            no_build,
            proxy_path,
            destination,
        } => install_proxy(
            profile.into(),
            target.as_deref(),
            no_build,
            proxy_path,
            destination,
        ),
        Command::Mods { command } => mods(command),
        Command::SyncAbi => sync_abi(),
    }
}

fn workspace_root() -> Result<Utf8PathBuf> {
    let manifest_dir = Utf8PathBuf::from_path_buf(
        std::env::current_dir().context("failed to read current directory")?,
    )
    .map_err(|_| anyhow::anyhow!("current directory is not valid UTF-8"))?;

    if manifest_dir.ends_with("crates/xtask") {
        Ok(manifest_dir
            .parent()
            .and_then(Utf8Path::parent)
            .context("xtask manifest dir is malformed")?
            .to_path_buf())
    } else if manifest_dir.join("Cargo.toml").exists() {
        Ok(manifest_dir)
    } else {
        bail!("unable to determine workspace root from {}", manifest_dir)
    }
}

fn sync_abi() -> Result<()> {
    let root = workspace_root()?;
    write_if_changed(
        &root.join(
            "crates/ue4ssl-cpp-support/vendor/UE4SSL/include/Compat/GeneratedRustCoreAbi.hpp",
        ),
        &ue4ssl_abi::render_rustcore_header(),
    )?;
    write_if_changed(
        &root.join("crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Compat/GeneratedPsScanAbi.hpp"),
        &ue4ssl_abi::render_scan_header(),
    )?;
    write_if_changed(
        &root.join("crates/ue4ssl-cpp-support/vendor/UE4SSL/include/Compat/GeneratedHostAbi.hpp"),
        &ue4ssl_abi::render_host_header(),
    )?;
    Ok(())
}

fn mods(command: ModsCommand) -> Result<()> {
    match command {
        ModsCommand::List => {
            let root = workspace_root()?;
            for spec in discover_native_mods(&root)? {
                println!(
                    "{}\t{}\t{} sources",
                    spec.mod_name,
                    spec.package_name,
                    spec.source_files.len()
                );
            }
            Ok(())
        }
    }
}

fn build(profile: CargoProfile, target: Option<&str>, scope: BuildScope) -> Result<()> {
    sync_abi()?;

    let root = workspace_root()?;
    let shell = Shell::new()?;
    prepare_cargo_shell(&shell, &root);

    build_artifacts(&shell, profile, target, core_artifacts(), "core artifacts")?;
    if scope.core_only {
        return Ok(());
    }

    if !scope.only_mods() {
        build_artifacts(
            &shell,
            profile,
            target,
            runtime_artifacts(),
            "runtime artifacts",
        )?;
    }

    build_native_mods(&shell, &root, profile, target, &scope.mod_names)?;
    Ok(())
}

fn build_artifacts(
    shell: &Shell,
    profile: CargoProfile,
    target: Option<&str>,
    artifacts: impl IntoIterator<Item = &'static ArtifactSpec>,
    label: &str,
) -> Result<()> {
    let mut package_args = Vec::new();
    for artifact in artifacts {
        package_args.push("-p".to_string());
        package_args.push(artifact.package_name.to_string());
    }

    if package_args.is_empty() {
        return Ok(());
    }

    run_cargo_build(shell, profile, target, &package_args, label)
}

fn build_native_mods(
    shell: &Shell,
    root: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
    requested_names: &[String],
) -> Result<()> {
    let discovered = discover_native_mods(root)?;
    let selected = select_native_mods(&discovered, requested_names)?;
    if selected.is_empty() {
        return Ok(());
    }

    let generated_workspace = write_generated_mod_workspace(root, profile, target, &selected)?;
    let mut package_args = vec![
        "--manifest-path".to_owned(),
        generated_workspace.join("Cargo.toml").to_string(),
    ];
    for spec in selected {
        package_args.push("-p".to_owned());
        package_args.push(spec.package_name.clone());
    }

    run_cargo_build(shell, profile, target, &package_args, "native mods")
}

fn build_native_support(profile: CargoProfile, target: Option<&str>) -> Result<()> {
    let root = workspace_root()?;
    let shell = Shell::new()?;
    prepare_cargo_shell(&shell, &root);
    let package_args = ["-p", "ue4ssl-unreal-support", "-p", "ue4ssl-support"];

    let package_args = package_args
        .iter()
        .map(|value| value.to_string())
        .collect::<Vec<_>>();
    run_cargo_build(
        &shell,
        profile,
        target,
        &package_args,
        "native support crates",
    )
}

fn build_proxy(
    profile: CargoProfile,
    target: Option<&str>,
    proxy_path: Option<Utf8PathBuf>,
) -> Result<()> {
    let root = workspace_root()?;
    let shell = Shell::new()?;
    prepare_cargo_shell(&shell, &root);

    if let Some(proxy_path) = proxy_path {
        shell.set_var("UE4SSL_PROXY_PATH", proxy_path.as_str());
    }

    let package_args = vec!["-p".to_owned(), "ue4ssl-proxy".to_owned()];
    run_cargo_build(&shell, profile, target, &package_args, "ue4ssl-proxy")
}

fn package(
    profile: CargoProfile,
    target: Option<&str>,
    no_build: bool,
    scope: BuildScope,
) -> Result<()> {
    package_to_stage(profile, target, no_build, scope).map(|_| ())
}

fn install(
    profile: CargoProfile,
    target: Option<&str>,
    no_build: bool,
    scope: BuildScope,
    destination: Utf8PathBuf,
) -> Result<()> {
    let stage_dir = package_to_stage(profile, target, no_build, scope)?;
    copy_tree(&stage_dir, &destination)?;
    Ok(())
}

fn package_proxy(
    profile: CargoProfile,
    target: Option<&str>,
    no_build: bool,
    proxy_path: Option<Utf8PathBuf>,
) -> Result<Utf8PathBuf> {
    let proxy_path = proxy_path.unwrap_or_else(default_proxy_path);
    if !no_build {
        build_proxy(profile, target, Some(proxy_path.clone()))?;
    }

    let root = workspace_root()?;
    let stage_dir = package_profile_dir(&root, profile, target).join("proxy");
    if stage_dir.exists() {
        fs::remove_dir_all(stage_dir.as_std_path()).with_context(|| {
            format!("failed to remove existing proxy package dir {}", stage_dir)
        })?;
    }
    fs::create_dir_all(stage_dir.as_std_path())
        .with_context(|| format!("failed to create proxy package dir {}", stage_dir))?;

    let proxy_stem = proxy_path
        .file_stem()
        .context("proxy path is missing file stem")?;
    let built_binary = artifact_binary_path(&root, profile, target, "ue4ssl_proxy");
    if !built_binary.exists() {
        bail!("expected built proxy artifact at {}", built_binary);
    }
    copy_file(&built_binary, &stage_dir.join(format!("{proxy_stem}.dll")))?;

    let built_import_lib = artifact_import_lib_path(&root, profile, target, "ue4ssl_proxy");
    if built_import_lib.exists() {
        copy_file(
            &built_import_lib,
            &stage_dir.join(format!("{proxy_stem}.dll.lib")),
        )?;
    }

    let built_pdb = artifact_pdb_path(&root, profile, target, "ue4ssl_proxy");
    if built_pdb.exists() {
        copy_file(&built_pdb, &stage_dir.join(format!("{proxy_stem}.pdb")))?;
    }

    Ok(stage_dir)
}

fn install_proxy(
    profile: CargoProfile,
    target: Option<&str>,
    no_build: bool,
    proxy_path: Option<Utf8PathBuf>,
    destination: Utf8PathBuf,
) -> Result<()> {
    let stage_dir = package_proxy(profile, target, no_build, proxy_path)?;
    copy_tree(&stage_dir, &destination)?;
    Ok(())
}

fn package_to_stage(
    profile: CargoProfile,
    target: Option<&str>,
    no_build: bool,
    scope: BuildScope,
) -> Result<Utf8PathBuf> {
    if !no_build {
        build(profile, target, scope.clone())?;
    }

    let root = workspace_root()?;
    let stage_dir = package_stage_dir(&root, profile, target);
    if stage_dir.exists() {
        fs::remove_dir_all(stage_dir.as_std_path())
            .with_context(|| format!("failed to remove existing package dir {}", stage_dir))?;
    }
    fs::create_dir_all(stage_dir.as_std_path())
        .with_context(|| format!("failed to create package dir {}", stage_dir))?;

    let discovered = discover_native_mods(&root)?;
    let selected_mods = if scope.core_only {
        Vec::new()
    } else {
        select_native_mods(&discovered, &scope.mod_names)?
    };

    if scope.core_only {
        for artifact in core_artifacts() {
            stage_artifact(&root, &stage_dir, profile, target, artifact)?;
        }
    } else if scope.only_mods() {
        for spec in selected_mods {
            stage_native_mod(&root, &stage_dir, profile, target, spec)?;
        }
    } else {
        for artifact in selected_artifacts(false) {
            stage_artifact(&root, &stage_dir, profile, target, artifact)?;
        }
        for spec in selected_mods {
            stage_native_mod(&root, &stage_dir, profile, target, spec)?;
        }
    }

    Ok(stage_dir)
}

fn stage_artifact(
    root: &Utf8Path,
    stage_dir: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
    artifact: &ArtifactSpec,
) -> Result<()> {
    let cargo_binary = artifact_binary_path(root, profile, target, artifact.cargo_target_stem);
    if !cargo_binary.exists() {
        bail!("expected built artifact at {}", cargo_binary);
    }

    match artifact.kind {
        PackageKind::CoreDll => {
            copy_file(
                &cargo_binary,
                &stage_dir.join(format!("{}.dll", artifact.binary_name)),
            )?;

            let import_lib = artifact_import_lib_path(root, profile, target, artifact.binary_name);
            if import_lib.exists() {
                copy_file(
                    &import_lib,
                    &stage_dir.join(format!("{}.dll.lib", artifact.binary_name)),
                )?;
            }

            let pdb = artifact_pdb_path(root, profile, target, artifact.cargo_target_stem);
            if pdb.exists() {
                copy_file(
                    &pdb,
                    &stage_dir.join(format!("{}.pdb", artifact.binary_name)),
                )?;
            }
        }
        PackageKind::ScriptEngine => {
            let mod_name = artifact
                .mod_directory_name
                .context("mod artifact missing directory name")?;
            let mod_dir = stage_dir.join("mods").join(mod_name);
            copy_file(&cargo_binary, &mod_dir.join("main.dll"))?;

            let pdb = artifact_pdb_path(root, profile, target, artifact.cargo_target_stem);
            if pdb.exists() {
                copy_file(&pdb, &mod_dir.join("main.pdb"))?;
            }

            write_enabled_marker(&mod_dir)?;

            for extra_root in artifact.extra_stage_roots {
                stage_extra_root(root, &mod_dir, extra_root)?;
            }
        }
    }

    Ok(())
}

fn stage_native_mod(
    root: &Utf8Path,
    stage_dir: &Utf8Path,
    profile: CargoProfile,
    target: Option<&str>,
    spec: &NativeModSpec,
) -> Result<()> {
    let cargo_binary = built_native_mod_binary_path(root, profile, target, spec);
    if !cargo_binary.exists() {
        bail!("expected built native mod artifact at {}", cargo_binary);
    }

    let mod_dir = stage_dir.join("mods").join(&spec.mod_name);
    copy_file(&cargo_binary, &mod_dir.join("main.dll"))?;

    let pdb = built_native_mod_pdb_path(root, profile, target, spec);
    if pdb.exists() {
        copy_file(&pdb, &mod_dir.join("main.pdb"))?;
    }

    write_enabled_marker(&mod_dir)?;
    for resource_root in &spec.resource_roots {
        stage_resource_root(resource_root, &mod_dir)?;
    }

    Ok(())
}

fn write_enabled_marker(mod_dir: &Utf8Path) -> Result<()> {
    fs::create_dir_all(mod_dir.as_std_path())
        .with_context(|| format!("failed to create mod dir {}", mod_dir))?;
    let enabled = mod_dir.join("enabled.txt");
    if !enabled.exists() {
        fs::write(enabled.as_std_path(), b"")
            .with_context(|| format!("failed to write {}", enabled))?;
    }
    Ok(())
}

fn selected_artifacts(core_only: bool) -> Vec<&'static ArtifactSpec> {
    if core_only {
        core_artifacts().collect()
    } else {
        default_artifacts().collect()
    }
}

fn prepare_cargo_shell(shell: &Shell, root: &Utf8Path) {
    shell.change_dir(root.as_str());
    shell.set_var("CARGO_TARGET_DIR", root.join("target").as_str());
}

fn run_cargo_build(
    shell: &Shell,
    profile: CargoProfile,
    target: Option<&str>,
    package_args: &[String],
    label: &str,
) -> Result<()> {
    let target_args = cargo_target_args(target);
    if use_cargo_xwin(target) {
        ensure_cargo_xwin_available()?;
        match profile {
            CargoProfile::Dev => cmd!(shell, "cargo xwin build {target_args...} {package_args...}")
                .run()
                .with_context(|| format!("cargo xwin build failed for {label}"))?,
            CargoProfile::Release => cmd!(
                shell,
                "cargo xwin build --release {target_args...} {package_args...}"
            )
            .run()
            .with_context(|| format!("cargo xwin release build failed for {label}"))?,
        }
    } else {
        match profile {
            CargoProfile::Dev => cmd!(shell, "cargo build {target_args...} {package_args...}")
                .run()
                .with_context(|| format!("cargo build failed for {label}"))?,
            CargoProfile::Release => cmd!(
                shell,
                "cargo build --release {target_args...} {package_args...}"
            )
            .run()
            .with_context(|| format!("cargo release build failed for {label}"))?,
        }
    }

    Ok(())
}

fn cargo_target_args(target: Option<&str>) -> Vec<String> {
    match target.map(str::trim).filter(|value| !value.is_empty()) {
        Some(target) => vec!["--target".to_owned(), target.to_owned()],
        None => Vec::new(),
    }
}

fn use_cargo_xwin(target: Option<&str>) -> bool {
    !cfg!(windows)
        && target
            .map(str::trim)
            .is_some_and(|target| target == "x86_64-pc-windows-msvc")
}

fn ensure_cargo_xwin_available() -> Result<()> {
    if executable_in_path("cargo-xwin") {
        return Ok(());
    }

    bail!(
        "cross-compiling x86_64-pc-windows-msvc from this host requires cargo-xwin. Install it with `cargo install cargo-xwin` or run the underlying cargo build with a manually configured MSVC-compatible toolchain."
    )
}

fn executable_in_path(name: &str) -> bool {
    let Some(path) = env::var_os("PATH") else {
        return false;
    };

    env::split_paths(&path).any(|dir| dir.join(name).is_file())
}

fn stage_extra_root(root: &Utf8Path, mod_dir: &Utf8Path, extra_root: &str) -> Result<()> {
    let source = root.join(extra_root);
    if !source.exists() {
        bail!("expected extra stage root at {}", source);
    }

    let name = source
        .file_name()
        .context("extra stage root is missing file name")?;
    let destination = mod_dir.join(name);

    if source.is_dir() {
        copy_tree(&source, &destination)?;
    } else {
        copy_file(&source, &destination)?;
    }

    Ok(())
}

fn stage_resource_root(source: &Utf8Path, mod_dir: &Utf8Path) -> Result<()> {
    if source.is_dir() {
        copy_tree_contents(source, mod_dir)
    } else {
        let name = source.file_name().context("resource path is missing file name")?;
        copy_file(source, &mod_dir.join(name))
    }
}

fn copy_file(source: &Utf8Path, destination: &Utf8Path) -> Result<()> {
    if let Some(parent) = destination.parent() {
        fs::create_dir_all(parent.as_std_path())
            .with_context(|| format!("failed to create {}", parent))?;
    }

    fs::copy(source.as_std_path(), destination.as_std_path())
        .with_context(|| format!("failed to copy {} -> {}", source, destination))?;
    Ok(())
}

fn copy_tree_contents(source: &Utf8Path, destination: &Utf8Path) -> Result<()> {
    for entry in
        fs::read_dir(source.as_std_path()).with_context(|| format!("failed to read {}", source))?
    {
        let entry = entry.with_context(|| format!("failed to read entry under {}", source))?;
        let source_path = Utf8PathBuf::from_path_buf(entry.path())
            .map_err(|_| anyhow::anyhow!("non-UTF8 path under {}", source))?;
        let destination_path = destination.join(entry.file_name().to_string_lossy().as_ref());

        if entry
            .file_type()
            .with_context(|| format!("failed to stat {}", source_path))?
            .is_dir()
        {
            copy_tree(&source_path, &destination_path)?;
        } else {
            copy_file(&source_path, &destination_path)?;
        }
    }

    Ok(())
}

fn copy_tree(source: &Utf8Path, destination: &Utf8Path) -> Result<()> {
    fs::create_dir_all(destination.as_std_path())
        .with_context(|| format!("failed to create {}", destination))?;

    copy_tree_contents(source, destination)
}

fn default_proxy_path() -> Utf8PathBuf {
    Utf8PathBuf::from(r"C:\Windows\System32\dwmapi.dll")
}

fn write_if_changed(path: &Utf8Path, content: &str) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent.as_std_path())?;
    }

    match fs::read_to_string(path.as_std_path()) {
        Ok(existing) if existing == content => Ok(()),
        _ => {
            fs::write(path.as_std_path(), content)?;
            Ok(())
        }
    }
}
