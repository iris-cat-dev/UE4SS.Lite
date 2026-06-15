use camino::{Utf8Path, Utf8PathBuf};

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

    Mod,
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

pub const NATIVE_GROUPS: &[NativeGroup] = &[
    NativeGroup {
        name: "ue4ssl_native_support",

        strategy: NativeStrategy::BuildRsCc,

        legacy_targets: &[
            "ue4ssl_native_support",
            "Input",
            "Helpers",
            "DynamicOutput",
            "SinglePassSigScanner",
            "UE4SSHook",
        ],

        source_roots: &[
            "crates/ue4ssl-native-support/vendor/Input/src",
            "crates/ue4ssl-native-support/vendor/DynamicOutput/src",
            "crates/ue4ssl-native-support/vendor/SinglePassSigScanner/src",
        ],

        include_roots: &[
            "crates/ue4ssl-native-support/vendor/Input/include",
            "crates/ue4ssl-native-support/vendor/Common/include",
            "crates/ue4ssl-native-support/vendor/DynamicOutput/include",
            "crates/ue4ssl-native-support/vendor/SinglePassSigScanner/include",
            "crates/ue4ssl-hook/include",
        ],
    },
    NativeGroup {
        name: "ue4ssl_unreal_support",

        strategy: NativeStrategy::BuildRsCc,

        legacy_targets: &["Constructs", "Function", "Unreal"],

        source_roots: &["crates/ue4ssl-unreal-support/vendor/Unreal/src"],

        include_roots: &[
            "crates/ue4ssl-native-support/vendor/Common/include",
            "crates/ue4ssl-unreal-support/vendor/Function/include",
            "crates/ue4ssl-unreal-support/vendor/Unreal/include",
            "crates/ue4ssl-unreal-support/vendor/Unreal/generated_include",
        ],
    },
    NativeGroup {
        name: "ue4ssl_cpp_support",

        strategy: NativeStrategy::BuildRsCc,

        legacy_targets: &["UE4SSL", "MProgram"],

        source_roots: &["crates/ue4ssl-cpp-support/vendor/UE4SSL/src"],

        include_roots: &[
            "crates/ue4ssl-cpp-support/vendor/UE4SSL/include",
            "crates/ue4ssl-cpp-support/vendor/UE4SSL/generated_include",
            "crates/ue4ssl-native-support/vendor/Common/include",
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
        name: "ue4ssl_drg_native",

        strategy: NativeStrategy::BuildRsToolchain,

        legacy_targets: &["UE4SSL.DRG"],

        source_roots: &["Mods/UE4SSL.DRG/native/cpp"],

        include_roots: &[
            "Mods/UE4SSL.DRG/native/include",
            "Mods/UE4SSL.DRG/native/cpp",
        ],
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

pub const MOD_ARTIFACTS: &[ArtifactSpec] = &[
    ArtifactSpec {
        package_name: "mod-ue4ssl-drg",

        cargo_target_stem: "ue4ssl_drg",

        binary_name: "UE4SSL.DRG",

        kind: PackageKind::Mod,

        mod_directory_name: Some("UE4SSL.DRG"),

        extra_stage_roots: &[],
    },
    ArtifactSpec {
        package_name: "mod-ue4ssl-mintcat",

        cargo_target_stem: "ue4ssl_mintcat",

        binary_name: "UE4SSL.MintCat",

        kind: PackageKind::Mod,

        mod_directory_name: Some("UE4SSL.MintCat"),

        extra_stage_roots: &["Mods/UE4SSL.MintCat/config"],
    },
    ArtifactSpec {
        package_name: "mod-drg-audioreplace",

        cargo_target_stem: "ue4ssl_audioreplace",

        binary_name: "DRGMod.AudioReplace",

        kind: PackageKind::Mod,

        mod_directory_name: Some("DRGMod.AudioReplace"),

        extra_stage_roots: &[
            "Mods/DRGMod.AudioReplace/config",
            "Mods/DRGMod.AudioReplace/audio",
        ],
    },
    ArtifactSpec {
        package_name: "mod-roguecore-repeat-negotiation-cards-native",

        cargo_target_stem: "ue4ssl_roguecore_repeat",

        binary_name: "RogueCore.RepeatNegotiationCards.Native",

        kind: PackageKind::Mod,

        mod_directory_name: Some("RogueCore.RepeatNegotiationCards.Native"),

        extra_stage_roots: &[],
    },
];

pub fn artifact_by_package(package_name: &str) -> Option<&'static ArtifactSpec> {
    default_artifacts().find(|artifact| artifact.package_name == package_name)
}

pub fn default_artifacts() -> impl Iterator<Item = &'static ArtifactSpec> {
    CORE_ARTIFACTS.iter().chain(SCRIPT_ENGINE_ARTIFACTS.iter())
}

pub fn core_artifacts() -> impl Iterator<Item = &'static ArtifactSpec> {
    CORE_ARTIFACTS.iter()
}

pub fn runtime_artifacts() -> impl Iterator<Item = &'static ArtifactSpec> {
    SCRIPT_ENGINE_ARTIFACTS.iter()
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
