use std::{
    fs,
    path::PathBuf,
    process::{Command, Output},
    sync::atomic::{AtomicU64, Ordering},
};

static NEXT_FIXTURE: AtomicU64 = AtomicU64::new(0);

struct Workspace(PathBuf);

impl Workspace {
    fn new() -> Self {
        let root = std::env::temp_dir().join(format!(
            "ue4ssl-xtask-mods-{}-{}",
            std::process::id(),
            NEXT_FIXTURE.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir_all(&root).unwrap();
        let fixture = Self(root);
        fixture.write(
            "Cargo.toml",
            "[workspace]\nresolver = \"2\"\nmembers = [\"core\", \"Mods/cargo-owned\"]\n",
        );
        fixture.write(
            "core/Cargo.toml",
            "[package]\nname = \"fixture-core\"\nversion = \"0.1.0\"\nedition = \"2021\"\n",
        );
        fixture.write("core/src/lib.rs", "");
        fixture.write("Mods/cargo-owned/Cargo.toml", "[package]\nname = \"fixture-package\"\nversion = \"0.1.0\"\nedition = \"2021\"\n[lib]\nname = \"custom_output\"\ncrate-type = [\"cdylib\", \"rlib\"]\n");
        fixture.write("Mods/cargo-owned/src/lib.rs", "");
        fixture.write(
            "Mods/cargo-owned/native/cpp/owned.cpp",
            "// Cargo owns these sources\n",
        );
        fixture.write(
            "Mods/cargo-owned/mod.json",
            r#"{"name":"Logical.Cargo","resources":["config"]}"#,
        );
        fixture.write("Mods/cargo-owned/config/settings.ini", "setting=value\n");
        fixture.write("Mods/cargo-owned/resources/overlay.txt", "cargo overlay");
        fixture.write(
            "Mods/Native/native/cpp/main.cpp",
            "// generated native source\n",
        );
        fixture.write("Mods/Native/resources/native.txt", "native overlay");
        for stem in [
            "custom_output",
            "ue4ssl_mod_native",
            "UE4SSL",
            "ue4ssl_javascript",
            "ue4ssl_lua",
        ] {
            fixture.write(&format!("target/fixture-target/release/{stem}.dll"), stem);
        }
        fixture
    }

    fn write(&self, path: &str, contents: &str) {
        let path = self.0.join(path);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, contents).unwrap();
    }

    fn run(&self, args: &[&str]) -> Output {
        Command::new(env!("CARGO_BIN_EXE_xtask"))
            .current_dir(&self.0)
            .args(args)
            .output()
            .unwrap()
    }

    fn succeeds(&self, args: &[&str]) -> Output {
        let output = self.run(args);
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        output
    }

    fn stage(&self) -> PathBuf {
        self.0.join("target/package/fixture-target/release/ue4ss")
    }
}

impl Drop for Workspace {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

#[test]
fn mixed_backends_share_selection_and_resource_staging() {
    let fixture = Workspace::new();
    let listing = fixture.succeeds(&["mods", "list"]);
    let listing = String::from_utf8(listing.stdout).unwrap();
    let mut names = listing
        .lines()
        .map(|line| line.split('\t').next().unwrap())
        .collect::<Vec<_>>();
    names.sort();
    assert_eq!(names, ["Logical.Cargo", "Native"]);
    assert!(listing.contains("Logical.Cargo\tcargo\tfixture-package"));
    assert!(listing.contains("Native\tgenerated-native\t"));

    fixture.succeeds(&[
        "package",
        "--no-build",
        "--profile",
        "release",
        "--target",
        "fixture-target",
    ]);
    let stage = fixture.stage();
    assert!(stage.join("UE4SSL.dll").is_file());
    assert!(stage.join("mods/UE4SSL.JavaScript/main.dll").is_file());
    assert!(stage.join("mods/UE4SSL.Lua/main.dll").is_file());
    assert_eq!(
        fs::read_to_string(stage.join("mods/Logical.Cargo/main.dll")).unwrap(),
        "custom_output"
    );
    assert_eq!(
        fs::read_to_string(stage.join("mods/Logical.Cargo/config/settings.ini")).unwrap(),
        "setting=value\n"
    );
    assert_eq!(
        fs::read_to_string(stage.join("mods/Logical.Cargo/overlay.txt")).unwrap(),
        "cargo overlay"
    );
    assert_eq!(
        fs::read_to_string(stage.join("mods/Native/native.txt")).unwrap(),
        "native overlay"
    );
    assert!(stage.join("mods/Logical.Cargo/enabled.txt").is_file());

    fixture.succeeds(&[
        "package",
        "--no-build",
        "--mods-only",
        "--profile",
        "release",
        "--target",
        "fixture-target",
    ]);
    assert!(!stage.join("UE4SSL.dll").exists());
    assert!(!stage.join("mods/UE4SSL.Lua").exists());
    assert!(stage.join("mods/Native/main.dll").is_file());
    assert!(stage.join("mods/Logical.Cargo/main.dll").is_file());

    fixture.succeeds(&[
        "install",
        "--no-build",
        "--mod",
        "logical.cargo",
        "--mod",
        "LOGICAL.CARGO",
        "--profile",
        "release",
        "--target",
        "fixture-target",
        "--destination",
        "installed",
    ]);
    assert!(!stage.join("mods/Native").exists());
    assert!(!stage.join("UE4SSL.dll").exists());
    assert_eq!(
        fs::read_to_string(
            fixture
                .0
                .join("installed/mods/Logical.Cargo/config/settings.ini")
        )
        .unwrap(),
        "setting=value\n"
    );

    fixture.succeeds(&[
        "package",
        "--no-build",
        "--core-only",
        "--profile",
        "release",
        "--target",
        "fixture-target",
    ]);
    assert!(stage.join("UE4SSL.dll").is_file());
    assert!(!stage.join("mods").exists());
}

#[test]
fn invalid_selection_does_not_build_or_destroy_staging() {
    let fixture = Workspace::new();
    fixture.write("target/package/debug/ue4ss/keep.txt", "previous package");
    for args in [
        vec!["build", "--mod", "missing"],
        vec!["package", "--no-build", "--mod", "missing"],
        vec![
            "install",
            "--no-build",
            "--mod",
            "missing",
            "--destination",
            "installed",
        ],
    ] {
        let output = fixture.run(&args);
        assert!(!output.status.success());
        assert!(String::from_utf8_lossy(&output.stderr).contains("unknown Mod 'missing'"));
        assert_eq!(
            fs::read_to_string(fixture.0.join("target/package/debug/ue4ss/keep.txt")).unwrap(),
            "previous package"
        );
    }
    assert!(!fixture.0.join("installed").exists());
}

#[test]
fn cargo_mods_require_workspace_membership_and_cdylib() {
    let fixture = Workspace::new();
    fixture.write(
        "Cargo.toml",
        "[workspace]\nmembers = [\"core\"]\nexclude = [\"Mods/cargo-owned\"]\n",
    );
    let output = fixture.run(&["mods", "list"]);
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("not a workspace package"));

    fixture.write(
        "Cargo.toml",
        "[workspace]\nmembers = [\"core\", \"Mods/cargo-owned\"]\n",
    );
    fixture.write("Mods/cargo-owned/Cargo.toml", "[package]\nname = \"fixture-package\"\nversion = \"0.1.0\"\n[lib]\ncrate-type = [\"rlib\"]\n");
    let output = fixture.run(&["mods", "list"]);
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("requires a cdylib target"));
}
