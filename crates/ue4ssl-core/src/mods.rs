use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};

use super::contract::BUILTIN_MOD_LOAD_ORDER;

pub struct DiscoveredModSpec {
    pub mod_name: OsString,
    pub mod_path: PathBuf,
    pub dll_name: Option<OsString>,
    pub is_builtin: bool,
}

pub fn discover_mods(working_directory: &Path, mods_directory: &Path) -> Vec<DiscoveredModSpec> {
    let mut mods = Vec::new();

    for builtin in BUILTIN_MOD_LOAD_ORDER {
        let dll_path = working_directory.join(builtin.dll_name);
        if dll_path.exists() {
            mods.push(DiscoveredModSpec {
                mod_name: OsString::from(builtin.mod_name),
                mod_path: working_directory.to_path_buf(),
                dll_name: Some(OsString::from(builtin.dll_name)),
                is_builtin: true,
            });
        }
    }

    if let Ok(entries) = fs::read_dir(mods_directory) {
        for entry in entries.flatten() {
            let Ok(file_type) = entry.file_type() else {
                continue;
            };
            if !file_type.is_dir() {
                continue;
            }

            let lower_name = entry.file_name().to_string_lossy().to_lowercase();
            if lower_name == "shared" {
                continue;
            }

            let mod_path = entry.path();
            if mod_path.join("main.dll").exists() {
                mods.push(DiscoveredModSpec {
                    mod_name: entry.file_name(),
                    mod_path,
                    dll_name: None,
                    is_builtin: false,
                });
            }
        }
    }

    mods
}
