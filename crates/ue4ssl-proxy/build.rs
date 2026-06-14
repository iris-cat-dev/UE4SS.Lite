#[path = "../build_support/common.rs"]
mod common;

use std::collections::BTreeSet;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};

use cc::Build;
use common::{
    apply_common_msvc_flags, cc_archive_path, emit_dylib_link, emit_rerun_for_tree,
    require_paths_exist, whole_archive_flag, workspace_root_from_manifest_dir, BuildProfile,
};

#[derive(Clone, Debug)]
struct ExportFunction {
    ordinal: u16,
    is_named: bool,
    name: String,
}

#[derive(Clone, Copy, Debug)]
struct SectionHeader {
    virtual_address: u32,
    virtual_size: u32,
    raw_data_ptr: u32,
    raw_data_size: u32,
}

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_os != "windows" || target_env != "msvc" {
        panic!("ue4ssl-proxy currently supports only windows-msvc targets");
    }

    println!("cargo:rerun-if-env-changed=UE4SSL_PROXY_PATH");

    let manifest_dir =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("missing CARGO_MANIFEST_DIR"));
    let workspace_root = workspace_root_from_manifest_dir(&manifest_dir);
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("missing OUT_DIR"));
    let profile = BuildProfile::from_env();

    let proxy_src = workspace_root
        .join("crates")
        .join("ue4ssl-proxy")
        .join("src");
    require_paths_exist("ue4ssl-proxy", [&proxy_src]);
    emit_rerun_for_tree(&proxy_src);

    let proxy_path = env::var("UE4SSL_PROXY_PATH")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from(r"C:\Windows\System32\dwmapi.dll"));
    require_paths_exist("ue4ssl-proxy", [&proxy_path]);
    println!("cargo:rerun-if-changed={}", proxy_path.display());

    let exports = parse_exports(&proxy_path);
    let generated_dir = out_dir.join("generated_proxy");
    fs::create_dir_all(&generated_dir)
        .unwrap_or_else(|err| panic!("failed to create {}: {err}", generated_dir.display()));

    let dll_name = proxy_path
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or_else(|| panic!("proxy path is missing file name: {}", proxy_path.display()))
        .to_owned();
    let dll_stem = proxy_path
        .file_stem()
        .and_then(|name| name.to_str())
        .unwrap_or_else(|| panic!("proxy path is missing file stem: {}", proxy_path.display()))
        .to_owned();

    let def_path = generated_dir.join("proxy.def");
    let asm_path = generated_dir.join("proxy.asm");
    let cpp_path = generated_dir.join("dllmain.cpp");

    fs::write(&def_path, render_def_file(&dll_stem, &exports))
        .unwrap_or_else(|err| panic!("failed to write {}: {err}", def_path.display()));
    fs::write(&asm_path, render_asm_file(&exports))
        .unwrap_or_else(|err| panic!("failed to write {}: {err}", asm_path.display()));
    fs::write(&cpp_path, render_cpp_file(&dll_name, &exports))
        .unwrap_or_else(|err| panic!("failed to write {}: {err}", cpp_path.display()));

    compile_cpp_archive(&cpp_path, profile);
    compile_asm_archive(&asm_path);

    emit_dylib_link("user32");
    println!("cargo:rustc-link-arg-cdylib=/DEF:{}", def_path.display());
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&cc_archive_path(&out_dir, "ue4ssl_proxy_cpp"))
    );
    println!(
        "cargo:rustc-link-arg-cdylib={}",
        whole_archive_flag(&cc_archive_path(&out_dir, "ue4ssl_proxy_asm"))
    );
}

fn compile_cpp_archive(cpp_path: &Path, profile: BuildProfile) {
    let mut build = Build::new();
    apply_common_msvc_flags(&mut build, profile, true, "/std:c++23preview");
    build.file(cpp_path);
    build.compile("ue4ssl_proxy_cpp");
}

fn compile_asm_archive(asm_path: &Path) {
    let mut build = Build::new();
    build.file(asm_path);
    build.compile("ue4ssl_proxy_asm");
}

fn parse_exports(path: &Path) -> Vec<ExportFunction> {
    let bytes =
        fs::read(path).unwrap_or_else(|err| panic!("failed to read {}: {err}", path.display()));
    let pe_offset = read_u32(&bytes, 0x3c) as usize;
    if bytes.get(pe_offset..pe_offset + 4) != Some(b"PE\0\0".as_slice()) {
        panic!("{} is not a PE image", path.display());
    }

    let number_of_sections = read_u16(&bytes, pe_offset + 6) as usize;
    let optional_header_size = read_u16(&bytes, pe_offset + 20) as usize;
    let optional_header_offset = pe_offset + 24;
    let optional_magic = read_u16(&bytes, optional_header_offset);
    let export_directory_offset = match optional_magic {
        0x10b => optional_header_offset + 96,
        0x20b => optional_header_offset + 112,
        _ => panic!("unsupported PE optional header magic: 0x{optional_magic:04x}"),
    };

    let export_rva = read_u32(&bytes, export_directory_offset) as usize;
    if export_rva == 0 {
        return Vec::new();
    }

    let section_table_offset = optional_header_offset + optional_header_size;
    let sections = parse_sections(&bytes, section_table_offset, number_of_sections);
    let export_offset = rva_to_offset(&sections, &bytes, export_rva as u32) as usize;

    let base = read_u32(&bytes, export_offset + 16);
    let number_of_functions = read_u32(&bytes, export_offset + 20) as usize;
    let number_of_names = read_u32(&bytes, export_offset + 24) as usize;
    let address_of_functions = read_u32(&bytes, export_offset + 28);
    let address_of_names = read_u32(&bytes, export_offset + 32);
    let address_of_name_ordinals = read_u32(&bytes, export_offset + 36);

    let functions_offset = rva_to_offset(&sections, &bytes, address_of_functions) as usize;
    let names_offset = rva_to_offset(&sections, &bytes, address_of_names) as usize;
    let ordinals_offset = rva_to_offset(&sections, &bytes, address_of_name_ordinals) as usize;

    let mut exports = Vec::new();
    let mut named_ordinals = BTreeSet::new();

    for index in 0..number_of_names {
        let name_rva = read_u32(&bytes, names_offset + index * 4);
        let ordinal_index = read_u16(&bytes, ordinals_offset + index * 2) as u32;
        let ordinal = (base + ordinal_index) as u16;
        exports.push(ExportFunction {
            ordinal,
            is_named: true,
            name: read_c_string(&bytes, rva_to_offset(&sections, &bytes, name_rva) as usize),
        });
        named_ordinals.insert(ordinal);
    }

    for index in 0..number_of_functions {
        let ordinal = (base + index as u32) as u16;
        let function_rva = read_u32(&bytes, functions_offset + index * 4);
        if function_rva == 0 || named_ordinals.contains(&ordinal) {
            continue;
        }

        exports.push(ExportFunction {
            ordinal,
            is_named: false,
            name: format!("ordinal{ordinal}"),
        });
    }

    exports
}

fn parse_sections(
    bytes: &[u8],
    section_table_offset: usize,
    number_of_sections: usize,
) -> Vec<SectionHeader> {
    (0..number_of_sections)
        .map(|index| {
            let offset = section_table_offset + index * 40;
            SectionHeader {
                virtual_size: read_u32(bytes, offset + 8),
                virtual_address: read_u32(bytes, offset + 12),
                raw_data_size: read_u32(bytes, offset + 16),
                raw_data_ptr: read_u32(bytes, offset + 20),
            }
        })
        .collect()
}

fn rva_to_offset(sections: &[SectionHeader], bytes: &[u8], rva: u32) -> u32 {
    for section in sections {
        let span = section.virtual_size.max(section.raw_data_size);
        if rva >= section.virtual_address && rva < section.virtual_address.saturating_add(span) {
            return section.raw_data_ptr + (rva - section.virtual_address);
        }
    }

    if (rva as usize) < bytes.len() {
        rva
    } else {
        panic!("failed to resolve RVA 0x{rva:08x}");
    }
}

fn render_def_file(dll_stem: &str, exports: &[ExportFunction]) -> String {
    let mut out = String::new();
    out.push_str(&format!("LIBRARY {dll_stem}\n"));
    out.push_str("EXPORTS\n");
    for (index, export) in exports.iter().enumerate() {
        out.push_str(&format!(
            "  {}=f{} @{}\n",
            export.name, index, export.ordinal
        ));
    }
    out
}

fn render_asm_file(exports: &[ExportFunction]) -> String {
    let mut out = String::new();
    out.push_str(".code\n");
    out.push_str("extern mProcs:QWORD\n");
    for index in 0..exports.len() {
        out.push_str(&format!("f{index} proc\n"));
        out.push_str(&format!("  jmp mProcs[8*{index}]\n"));
        out.push_str(&format!("f{index} endp\n"));
    }
    out.push_str("end\n");
    out
}

fn render_cpp_file(dll_name: &str, exports: &[ExportFunction]) -> String {
    let dll_name = dll_name.replace('\\', "\\\\").replace('"', "\\\"");
    format!(
        r#"#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace fs = std::filesystem;

HMODULE SOriginalDll = nullptr;
extern "C" uintptr_t mProcs[{export_count}] = {{0}};

void setup_functions()
{{
{setup_lines}}}

void load_original_dll()
{{
    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    std::wstring dll_path = std::wstring(path) + L"\\{dll_name}";
    SOriginalDll = LoadLibraryW(dll_path.c_str());
    if (!SOriginalDll)
    {{
        MessageBoxW(nullptr, L"Failed to load proxy DLL", L"UE4SS Error", MB_OK | MB_ICONERROR);
        ExitProcess(0);
    }}
}}

bool is_absolute_path(const std::string& path)
{{
    return fs::path(path).is_absolute();
}}

HMODULE load_ue4ss_dll(HMODULE module_handle)
{{
    HMODULE hModule = nullptr;
    wchar_t moduleFilenameBuffer[1024]{{'\0'}};
    GetModuleFileNameW(module_handle, moduleFilenameBuffer, sizeof(moduleFilenameBuffer) / sizeof(wchar_t));
    const auto currentPath = fs::path(moduleFilenameBuffer).parent_path();
    const fs::path ue4ssPath = currentPath / "ue4ss" / "UE4SSL.dll";

    const fs::path overrideFilePath = currentPath / "override.txt";
    if (fs::exists(overrideFilePath))
    {{
        std::ifstream overrideFile(overrideFilePath);
        std::string overridePath;
        if (std::getline(overrideFile, overridePath))
        {{
            fs::path ue4ssOverridePath = overridePath;
            if (!is_absolute_path(overridePath))
            {{
                ue4ssOverridePath = currentPath / overridePath;
            }}

            ue4ssOverridePath = ue4ssOverridePath / "UE4SS.dll";
            hModule = LoadLibraryW(ue4ssOverridePath.c_str());
            if (hModule)
            {{
                return hModule;
            }}
        }}
    }}

    hModule = LoadLibraryW(ue4ssPath.c_str());
    if (!hModule)
    {{
        hModule = LoadLibraryW(L"UE4SSL.dll");
    }}

    return hModule;
}}

BOOL WINAPI DllMain(HMODULE hInstDll, DWORD fdwReason, LPVOID)
{{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {{
        load_original_dll();
        HMODULE hUE4SSDll = load_ue4ss_dll(hInstDll);
        if (hUE4SSDll)
        {{
            setup_functions();
        }}
        else
        {{
            MessageBoxW(nullptr, L"Failed to load UE4SSL.dll. Please see the docs on correct installation: https://docs.ue4ss.com/installation-guide", L"UE4SS Error", MB_OK | MB_ICONERROR);
            ExitProcess(0);
        }}
    }}
    else if (fdwReason == DLL_PROCESS_DETACH)
    {{
        FreeLibrary(SOriginalDll);
    }}
    return TRUE;
}}
"#,
        export_count = exports.len(),
        dll_name = dll_name,
        setup_lines = render_setup_lines(exports),
    )
}

fn render_setup_lines(exports: &[ExportFunction]) -> String {
    let mut lines = String::new();
    for (index, export) in exports.iter().enumerate() {
        if export.is_named {
            lines.push_str(&format!(
                "    mProcs[{index}] = reinterpret_cast<uintptr_t>(GetProcAddress(SOriginalDll, \"{}\"));\n",
                export.name.replace('\\', "\\\\").replace('"', "\\\""),
            ));
        } else {
            lines.push_str(&format!(
                "    mProcs[{index}] = reinterpret_cast<uintptr_t>(GetProcAddress(SOriginalDll, MAKEINTRESOURCEA({ordinal})));\n",
                ordinal = export.ordinal,
            ));
        }
    }
    lines
}

fn read_c_string(bytes: &[u8], offset: usize) -> String {
    let mut end = offset;
    while end < bytes.len() && bytes[end] != 0 {
        end += 1;
    }
    String::from_utf8_lossy(&bytes[offset..end]).into_owned()
}

fn read_u16(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes(
        bytes[offset..offset + 2]
            .try_into()
            .unwrap_or_else(|_| panic!("failed to read u16 at offset 0x{offset:x}")),
    )
}

fn read_u32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(
        bytes[offset..offset + 4]
            .try_into()
            .unwrap_or_else(|_| panic!("failed to read u32 at offset 0x{offset:x}")),
    )
}
