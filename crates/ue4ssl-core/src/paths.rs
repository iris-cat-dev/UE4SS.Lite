use std::fs;
use std::path::{Path, PathBuf};

#[derive(Default)]
pub struct ProgramPathSnapshot {
    pub root_directory: PathBuf,
    pub working_directory: PathBuf,
    pub mods_directory: PathBuf,
    pub game_executable_directory: PathBuf,
    pub settings_path_and_file: PathBuf,
    pub legacy_root_directory: PathBuf,
    pub object_dumper_output_directory: PathBuf,
    pub log_directory: PathBuf,
    pub game_path_and_exe_name: PathBuf,
    pub has_game_specific_config: bool,
}

pub fn compute_base_paths(module_file_path: &Path, game_exe_path: &Path) -> ProgramPathSnapshot {
    let root_directory = module_file_path
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_default();
    let game_executable_directory = game_exe_path
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_default();

    let mut snapshot = ProgramPathSnapshot {
        root_directory: root_directory.clone(),
        working_directory: root_directory.clone(),
        mods_directory: root_directory.join("Mods"),
        game_executable_directory: game_executable_directory.clone(),
        settings_path_and_file: root_directory.clone(),
        legacy_root_directory: game_executable_directory.clone(),
        object_dumper_output_directory: root_directory.clone(),
        log_directory: PathBuf::new(),
        game_path_and_exe_name: game_exe_path.to_path_buf(),
        has_game_specific_config: false,
    };

    let maybe_game_specific_name = game_executable_directory
        .ancestors()
        .nth(3)
        .and_then(Path::file_name)
        .map(|name| name.to_os_string());

    if let Some(game_specific_name) = maybe_game_specific_name {
        if let Ok(entries) = fs::read_dir(&root_directory) {
            for entry in entries.flatten() {
                let Ok(file_type) = entry.file_type() else {
                    continue;
                };
                if !file_type.is_dir() {
                    continue;
                }

                if entry.file_name() == game_specific_name {
                    snapshot.has_game_specific_config = true;
                    snapshot.working_directory = entry.path();
                    snapshot.mods_directory = entry.path().join("Mods");
                    snapshot.settings_path_and_file = entry.path();
                    snapshot.log_directory = snapshot.working_directory.clone();
                    snapshot.object_dumper_output_directory = snapshot.working_directory.clone();
                    snapshot.legacy_root_directory.push(entry.path());
                    break;
                }
            }
        }
    }

    snapshot.log_directory = snapshot.working_directory.clone();
    snapshot.settings_path_and_file.push("UE4SS-settings.ini");

    let legacy_settings = snapshot.legacy_root_directory.join("UE4SS-settings.ini");
    if legacy_settings.exists() && !snapshot.settings_path_and_file.exists() {
        snapshot.settings_path_and_file = legacy_settings;
    }

    let legacy_mods = snapshot.legacy_root_directory.join("Mods");
    if legacy_mods.exists() && !snapshot.mods_directory.exists() {
        snapshot.mods_directory = legacy_mods;
    }

    snapshot
}

pub fn resolve_mods_directory(
    working_directory: &Path,
    current_mods_directory: &Path,
    override_mods_directory: Option<&Path>,
) -> PathBuf {
    if let Some(override_mods_directory) = override_mods_directory {
        if override_mods_directory.is_relative() {
            working_directory.join(override_mods_directory)
        } else {
            override_mods_directory.to_path_buf()
        }
    } else if current_mods_directory.components().next().is_none() {
        working_directory.join("Mods")
    } else {
        current_mods_directory.to_path_buf()
    }
}
