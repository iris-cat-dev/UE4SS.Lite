#![allow(clippy::missing_safety_doc)]

use std::ffi::{c_char, c_void, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr::{copy_nonoverlapping, null_mut};
use std::slice;

use patternsleuth::iced_x86::{
    BlockEncoder, BlockEncoderOptions, Decoder, DecoderOptions, Instruction, InstructionBlock,
    Mnemonic, OpKind,
};

type Bool = i32;
type Dword = u32;
type Handle = *mut c_void;
type Hmodule = *mut c_void;
type Lpvoid = *mut c_void;
type Lpcvoid = *const c_void;
type Lpcwstr = *const u16;

const FALSE: Bool = 0;
const MEM_COMMIT: Dword = 0x0000_1000;
const MEM_RESERVE: Dword = 0x0000_2000;
const MEM_RELEASE: Dword = 0x0000_8000;
const PAGE_EXECUTE_READWRITE: Dword = 0x40;
const IMAGE_DIRECTORY_ENTRY_IMPORT: usize = 1;
const IMAGE_DOS_SIGNATURE: u16 = 0x5A4D;
const IMAGE_NT_SIGNATURE: u32 = 0x0000_4550;
const IMAGE_ORDINAL_FLAG64: u64 = 0x8000_0000_0000_0000;
const MAX_INSTRUCTION_SCAN: usize = 96;
const MAX_BRANCH_SCAN_DEPTH: usize = 16;
const DETOUR_STUB_SIZE: usize = 12;

#[link(name = "kernel32", kind = "raw-dylib")]
extern "system" {
    fn VirtualProtect(
        lp_address: Lpvoid,
        dw_size: usize,
        fl_new_protect: Dword,
        lpfl_old_protect: *mut Dword,
    ) -> Bool;
    fn VirtualAlloc(
        lp_address: Lpvoid,
        dw_size: usize,
        fl_allocation_type: Dword,
        fl_protect: Dword,
    ) -> Lpvoid;
    fn VirtualFree(lp_address: Lpvoid, dw_size: usize, dw_free_type: Dword) -> Bool;
    fn FlushInstructionCache(h_process: Handle, lp_base_address: Lpcvoid, dw_size: usize) -> Bool;
    fn ReadProcessMemory(
        h_process: Handle,
        lp_base_address: Lpcvoid,
        lp_buffer: Lpvoid,
        n_size: usize,
        lp_number_of_bytes_read: *mut usize,
    ) -> Bool;
    fn GetCurrentProcess() -> Handle;
    fn K32EnumProcessModules(
        h_process: Handle,
        lph_module: *mut Hmodule,
        cb: Dword,
        lpcb_needed: *mut Dword,
    ) -> Bool;
    fn GetModuleHandleW(lp_module_name: Lpcwstr) -> Hmodule;
}

#[repr(C)]
struct ImageDosHeader {
    e_magic: u16,
    e_cblp: u16,
    e_cp: u16,
    e_crlc: u16,
    e_cparhdr: u16,
    e_minalloc: u16,
    e_maxalloc: u16,
    e_ss: u16,
    e_sp: u16,
    e_csum: u16,
    e_ip: u16,
    e_cs: u16,
    e_lfarlc: u16,
    e_ovno: u16,
    e_res: [u16; 4],
    e_oemid: u16,
    e_oeminfo: u16,
    e_res2: [u16; 10],
    e_lfanew: i32,
}

#[repr(C)]
struct ImageFileHeader {
    machine: u16,
    number_of_sections: u16,
    time_date_stamp: u32,
    pointer_to_symbol_table: u32,
    number_of_symbols: u32,
    size_of_optional_header: u16,
    characteristics: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct ImageDataDirectory {
    virtual_address: u32,
    size: u32,
}

#[repr(C)]
struct ImageOptionalHeader64 {
    magic: u16,
    major_linker_version: u8,
    minor_linker_version: u8,
    size_of_code: u32,
    size_of_initialized_data: u32,
    size_of_uninitialized_data: u32,
    address_of_entry_point: u32,
    base_of_code: u32,
    image_base: u64,
    section_alignment: u32,
    file_alignment: u32,
    major_operating_system_version: u16,
    minor_operating_system_version: u16,
    major_image_version: u16,
    minor_image_version: u16,
    major_subsystem_version: u16,
    minor_subsystem_version: u16,
    win32_version_value: u32,
    size_of_image: u32,
    size_of_headers: u32,
    check_sum: u32,
    subsystem: u16,
    dll_characteristics: u16,
    size_of_stack_reserve: u64,
    size_of_stack_commit: u64,
    size_of_heap_reserve: u64,
    size_of_heap_commit: u64,
    loader_flags: u32,
    number_of_rva_and_sizes: u32,
    data_directory: [ImageDataDirectory; 16],
}

#[repr(C)]
struct ImageNtHeaders64 {
    signature: u32,
    file_header: ImageFileHeader,
    optional_header: ImageOptionalHeader64,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct ImageImportDescriptor {
    original_first_thunk: u32,
    time_date_stamp: u32,
    forwarder_chain: u32,
    name: u32,
    first_thunk: u32,
}

#[repr(C)]
union ImageThunkData64Union {
    forwarder_string: u64,
    function: u64,
    ordinal: u64,
    address_of_data: u64,
}

#[repr(C)]
struct ImageThunkData64 {
    u1: ImageThunkData64Union,
}

#[repr(C)]
struct ImageImportByName {
    hint: u16,
    name: [u8; 1],
}

#[repr(C)]
pub struct Ue4ssHookDetourHandle {
    inner: DetourHandle,
}

#[repr(C)]
pub struct Ue4ssHookIatHookHandle {
    inner: IatHookHandle,
}

struct DetourHandle {
    target: *mut u8,
    callback: *const u8,
    user_trampoline: *mut u64,
    trampoline: *mut u8,
    trampoline_allocation_size: usize,
    original_bytes: Vec<u8>,
    patch_size: usize,
    hooked: bool,
    scheme: u32,
}

struct IatHookHandle {
    dll_name: String,
    api_name: String,
    module_name: Vec<u16>,
    callback: u64,
    original: u64,
    user_original: *mut u64,
    thunk: *mut usize,
    hooked: bool,
}

fn ffi_ptr<T, F>(f: F) -> *mut T
where
    F: FnOnce() -> Option<*mut T>,
{
    catch_unwind(AssertUnwindSafe(f))
        .ok()
        .flatten()
        .unwrap_or(null_mut())
}

fn ffi_bool<F>(f: F) -> bool
where
    F: FnOnce() -> bool,
{
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(false)
}

unsafe fn read_c_string(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    Some(CStr::from_ptr(ptr).to_string_lossy().into_owned())
}

unsafe fn read_wide_string(ptr: *const u16) -> Vec<u16> {
    if ptr.is_null() || *ptr == 0 {
        return Vec::new();
    }

    let mut len = 0usize;
    while *ptr.add(len) != 0 {
        len += 1;
    }

    let mut out = slice::from_raw_parts(ptr, len).to_vec();
    out.push(0);
    out
}

unsafe fn read_process_bytes(address: *const u8, len: usize) -> &'static [u8] {
    slice::from_raw_parts(address, len)
}

fn absolute_jump_bytes(destination: u64) -> [u8; DETOUR_STUB_SIZE] {
    let mut bytes = [0u8; DETOUR_STUB_SIZE];
    bytes[0] = 0x48;
    bytes[1] = 0xB8;
    bytes[2..10].copy_from_slice(&destination.to_le_bytes());
    bytes[10] = 0xFF;
    bytes[11] = 0xE0;
    bytes
}

unsafe fn flush_instruction_cache(address: *const u8, size: usize) {
    let _ = FlushInstructionCache(GetCurrentProcess(), address.cast(), size);
}

unsafe fn write_executable_memory(address: *mut u8, data: &[u8]) -> bool {
    let mut old_protect = 0u32;
    if VirtualProtect(
        address.cast(),
        data.len(),
        PAGE_EXECUTE_READWRITE,
        &mut old_protect,
    ) == FALSE
    {
        return false;
    }

    copy_nonoverlapping(data.as_ptr(), address, data.len());
    flush_instruction_cache(address, data.len());

    let mut unused = 0u32;
    let _ = VirtualProtect(address.cast(), data.len(), old_protect, &mut unused);
    true
}

unsafe fn patch_target(address: *mut u8, patch: &[u8], total_len: usize) -> bool {
    let mut data = vec![0x90u8; total_len];
    data[..patch.len()].copy_from_slice(patch);
    write_executable_memory(address, &data)
}

unsafe fn allocate_executable(size: usize) -> *mut u8 {
    VirtualAlloc(
        null_mut(),
        size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE,
    )
    .cast()
}

unsafe fn free_executable(address: *mut u8) {
    if !address.is_null() {
        let _ = VirtualFree(address.cast(), 0, MEM_RELEASE);
    }
}

unsafe fn decode_first_instruction(address: *const u8, max_len: usize) -> Option<Instruction> {
    let bytes = read_process_bytes(address, max_len);
    let mut decoder = Decoder::with_ip(64, bytes, address as u64, DecoderOptions::NONE);
    let instruction = decoder.decode();
    if instruction.is_invalid() {
        None
    } else {
        Some(instruction)
    }
}

fn resolve_instruction_target(instruction: &Instruction) -> Option<*mut c_void> {
    match instruction.op0_kind() {
        OpKind::NearBranch16 | OpKind::NearBranch32 | OpKind::NearBranch64 => {
            Some(instruction.near_branch_target() as *mut c_void)
        }
        OpKind::Memory if instruction.is_ip_rel_memory_operand() => unsafe {
            let target_ptr = instruction.ip_rel_memory_address() as *const u64;
            if target_ptr.is_null() {
                None
            } else {
                let target = *target_ptr;
                if target == 0 {
                    None
                } else {
                    Some(target as *mut c_void)
                }
            }
        },
        _ => None,
    }
}

unsafe fn resolve_branch_if_matches(address: *mut c_void, expected: Mnemonic) -> *mut c_void {
    let Some(instruction) = decode_first_instruction(address.cast(), 16) else {
        return null_mut();
    };

    if instruction.mnemonic() != expected {
        return null_mut();
    }

    resolve_instruction_target(&instruction).unwrap_or(null_mut())
}

unsafe fn resolve_jmp_chain(address: *mut c_void, depth: usize) -> *mut c_void {
    if address.is_null() || depth >= MAX_BRANCH_SCAN_DEPTH {
        return address;
    }

    let Some(instruction) = decode_first_instruction(address.cast(), 16) else {
        return address;
    };

    if instruction.mnemonic() != Mnemonic::Jmp {
        return address;
    }

    let Some(target) = resolve_instruction_target(&instruction) else {
        return null_mut();
    };

    if target == address {
        target
    } else {
        resolve_jmp_chain(target, depth + 1)
    }
}

unsafe fn find_nth_call_target_internal(
    function_ptr: *mut c_void,
    max_bytes: usize,
    nth_call: u32,
) -> *mut c_void {
    if function_ptr.is_null() || nth_call == 0 {
        return null_mut();
    }

    let bytes = read_process_bytes(function_ptr.cast(), max_bytes);
    let mut decoder = Decoder::with_ip(64, bytes, function_ptr as u64, DecoderOptions::NONE);
    let mut seen = 0u32;

    while decoder.can_decode() {
        let instruction = decoder.decode();
        if instruction.is_invalid() {
            break;
        }

        if instruction.mnemonic() == Mnemonic::Call {
            seen += 1;
            if seen == nth_call {
                let Some(target) = resolve_instruction_target(&instruction) else {
                    return null_mut();
                };
                return resolve_jmp_chain(target, 0);
            }
        }
    }

    null_mut()
}

unsafe fn resolve_jmp_target_or_self_internal(
    function_ptr: *mut c_void,
    max_bytes: usize,
) -> *mut c_void {
    let Some(instruction) = decode_first_instruction(function_ptr.cast(), max_bytes) else {
        return null_mut();
    };

    if instruction.mnemonic() == Mnemonic::Jmp {
        resolve_jmp_chain(function_ptr, 0)
    } else {
        function_ptr
    }
}

unsafe fn decode_overwritten_instructions(
    target: *const u8,
    min_patch_size: usize,
) -> Option<(Vec<Instruction>, Vec<u8>, usize)> {
    let bytes = read_process_bytes(target, MAX_INSTRUCTION_SCAN);
    let mut decoder = Decoder::with_ip(64, bytes, target as u64, DecoderOptions::NONE);
    let mut instructions = Vec::new();
    let mut total = 0usize;

    while total < min_patch_size {
        let instruction = decoder.decode();
        if instruction.is_invalid() {
            return None;
        }

        total += instruction.len();
        instructions.push(instruction);
    }

    Some((instructions, bytes[..total].to_vec(), total))
}

unsafe fn encode_trampoline(
    trampoline: *mut u8,
    instructions: &[Instruction],
    resume_address: u64,
) -> Option<Vec<u8>> {
    let result = BlockEncoder::encode(
        64,
        InstructionBlock::new(instructions, trampoline as u64),
        BlockEncoderOptions::NONE,
    )
    .ok()?;

    let mut buffer = result.code_buffer;
    buffer.extend_from_slice(&absolute_jump_bytes(resume_address));
    Some(buffer)
}

impl DetourHandle {
    unsafe fn new(target: u64, callback: u64, user_trampoline: *mut u64) -> Option<Self> {
        if target == 0 || callback == 0 || user_trampoline.is_null() {
            return None;
        }

        *user_trampoline = 0;

        Some(Self {
            target: target as *mut u8,
            callback: callback as *const u8,
            user_trampoline,
            trampoline: null_mut(),
            trampoline_allocation_size: 0,
            original_bytes: Vec::new(),
            patch_size: 0,
            hooked: false,
            scheme: 0,
        })
    }

    unsafe fn hook(&mut self) -> bool {
        if self.hooked {
            return true;
        }

        let (instructions, original_bytes, patch_size) =
            match decode_overwritten_instructions(self.target, DETOUR_STUB_SIZE) {
                Some(value) => value,
                None => return false,
            };

        let trampoline_allocation_size = 0x1000usize;
        let trampoline = allocate_executable(trampoline_allocation_size);
        if trampoline.is_null() {
            return false;
        }

        let resume_address = self.target as u64 + patch_size as u64;
        let trampoline_bytes = match encode_trampoline(trampoline, &instructions, resume_address) {
            Some(bytes) => bytes,
            None => {
                free_executable(trampoline);
                return false;
            }
        };

        if trampoline_bytes.len() > trampoline_allocation_size
            || !write_executable_memory(trampoline, &trampoline_bytes)
        {
            free_executable(trampoline);
            return false;
        }

        // Publish the trampoline pointer BEFORE patching the target.
        // Otherwise, another thread can enter the function between
        // patch_target() and the trampoline store, observe Trampoline == 0
        // inside TDetourInstance::Invoke, and crash calling NULL.
        self.original_bytes = original_bytes;
        self.patch_size = patch_size;
        self.trampoline = trampoline;
        self.trampoline_allocation_size = trampoline_allocation_size;
        core::ptr::write_volatile(self.user_trampoline, trampoline as u64);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);

        let patch = absolute_jump_bytes(self.callback as u64);
        if !patch_target(self.target, &patch, patch_size) {
            core::ptr::write_volatile(self.user_trampoline, 0u64);
            free_executable(trampoline);
            self.trampoline = null_mut();
            self.trampoline_allocation_size = 0;
            self.original_bytes = Vec::new();
            self.patch_size = 0;
            return false;
        }

        self.hooked = true;
        true
    }

    unsafe fn unhook(&mut self) -> bool {
        if !self.hooked {
            return false;
        }

        if !write_executable_memory(self.target, &self.original_bytes) {
            return false;
        }

        free_executable(self.trampoline);
        self.trampoline = null_mut();
        self.trampoline_allocation_size = 0;
        self.original_bytes.clear();
        self.patch_size = 0;
        *self.user_trampoline = 0;
        self.hooked = false;
        true
    }
}

impl Drop for DetourHandle {
    fn drop(&mut self) {
        unsafe {
            let _ = self.unhook();
        }
    }
}

impl IatHookHandle {
    unsafe fn new(
        dll_name: *const c_char,
        api_name: *const c_char,
        callback: u64,
        user_original: *mut u64,
        module_name: *const u16,
    ) -> Option<Self> {
        let dll_name = read_c_string(dll_name)?;
        let api_name = read_c_string(api_name)?;
        if callback == 0 || user_original.is_null() {
            return None;
        }

        *user_original = 0;

        Some(Self {
            dll_name,
            api_name,
            module_name: read_wide_string(module_name),
            callback,
            original: 0,
            user_original,
            thunk: null_mut(),
            hooked: false,
        })
    }

    unsafe fn hook(&mut self) -> bool {
        if self.hooked {
            return true;
        }

        let Some(thunk) = self.find_iat_thunk() else {
            return false;
        };

        self.original = *thunk as u64;
        if !write_pointer_value(thunk, self.callback as usize) {
            return false;
        }

        self.thunk = thunk;
        *self.user_original = self.original;
        self.hooked = true;
        true
    }

    unsafe fn unhook(&mut self) -> bool {
        if !self.hooked || self.thunk.is_null() {
            return false;
        }

        if !write_pointer_value(self.thunk, self.original as usize) {
            return false;
        }

        *self.user_original = 0;
        self.thunk = null_mut();
        self.original = 0;
        self.hooked = false;
        true
    }

    unsafe fn find_iat_thunk(&self) -> Option<*mut usize> {
        if !self.module_name.is_empty() {
            let module = GetModuleHandleW(self.module_name.as_ptr());
            if module.is_null() {
                return None;
            }
            return find_iat_thunk_in_module(module, &self.dll_name, &self.api_name);
        }

        let process = GetCurrentProcess();
        let mut needed = 0u32;
        if K32EnumProcessModules(process, null_mut(), 0, &mut needed) == FALSE || needed == 0 {
            return None;
        }

        let count = needed as usize / std::mem::size_of::<Hmodule>();
        let mut modules = vec![null_mut(); count];
        if K32EnumProcessModules(process, modules.as_mut_ptr(), needed, &mut needed) == FALSE {
            return None;
        }

        for module in modules {
            if let Some(thunk) = find_iat_thunk_in_module(module, &self.dll_name, &self.api_name) {
                return Some(thunk);
            }
        }

        None
    }
}

impl Drop for IatHookHandle {
    fn drop(&mut self) {
        unsafe {
            let _ = self.unhook();
        }
    }
}

unsafe fn write_pointer_value(address: *mut usize, value: usize) -> bool {
    let bytes = value.to_ne_bytes();
    write_executable_memory(address.cast(), &bytes)
}

fn eq_ignore_ascii_case(left: &str, right: &str) -> bool {
    left.eq_ignore_ascii_case(right)
}

unsafe fn find_iat_thunk_in_module(
    module_base: Hmodule,
    dll_name: &str,
    api_name: &str,
) -> Option<*mut usize> {
    if module_base.is_null() {
        return None;
    }

    let base = module_base as usize;
    let dos_header = &*(base as *const ImageDosHeader);
    if dos_header.e_magic != IMAGE_DOS_SIGNATURE {
        return None;
    }

    let nt_header = &*((base + dos_header.e_lfanew as usize) as *const ImageNtHeaders64);
    if nt_header.signature != IMAGE_NT_SIGNATURE {
        return None;
    }

    let import_directory = nt_header.optional_header.data_directory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if import_directory.virtual_address == 0 {
        return None;
    }

    let mut descriptor_ptr =
        (base + import_directory.virtual_address as usize) as *const ImageImportDescriptor;

    while (*descriptor_ptr).name != 0 {
        let descriptor = *descriptor_ptr;
        let imported_dll = CStr::from_ptr((base + descriptor.name as usize) as *const c_char)
            .to_string_lossy()
            .into_owned();

        if eq_ignore_ascii_case(&imported_dll, dll_name) {
            if descriptor.original_first_thunk == 0 {
                return None;
            }

            let mut original_thunk =
                (base + descriptor.original_first_thunk as usize) as *const ImageThunkData64;
            let mut thunk = (base + descriptor.first_thunk as usize) as *mut usize;

            loop {
                let ordinal = (*original_thunk).u1.ordinal;
                if ordinal == 0 {
                    break;
                }

                if ordinal & IMAGE_ORDINAL_FLAG64 == 0 {
                    let import = (base + (*original_thunk).u1.address_of_data as usize)
                        as *const ImageImportByName;
                    let name_ptr = std::ptr::addr_of!((*import).name) as *const c_char;
                    let imported_name = CStr::from_ptr(name_ptr).to_string_lossy();
                    if imported_name == api_name {
                        return Some(thunk);
                    }
                }

                original_thunk = original_thunk.add(1);
                thunk = thunk.add(1);
            }
        }

        descriptor_ptr = descriptor_ptr.add(1);
    }

    None
}

#[no_mangle]
pub extern "C" fn ue4ss_asm_resolve_jmp(instruction_ptr: *mut c_void) -> *mut c_void {
    ffi_ptr(|| Some(unsafe { resolve_branch_if_matches(instruction_ptr, Mnemonic::Jmp) }))
}

#[no_mangle]
pub extern "C" fn ue4ss_asm_resolve_call(instruction_ptr: *mut c_void) -> *mut c_void {
    ffi_ptr(|| Some(unsafe { resolve_branch_if_matches(instruction_ptr, Mnemonic::Call) }))
}

#[no_mangle]
pub extern "C" fn ue4ss_asm_resolve_function_address_from_potential_jmp(
    function_ptr: *mut c_void,
) -> *mut c_void {
    ffi_ptr(|| Some(unsafe { resolve_jmp_chain(function_ptr, 0) }))
}

#[no_mangle]
pub extern "C" fn ue4ss_asm_find_nth_call_target(
    function_ptr: *mut c_void,
    max_bytes: u64,
    nth_call: u32,
) -> *mut c_void {
    ffi_ptr(|| {
        Some(unsafe { find_nth_call_target_internal(function_ptr, max_bytes as usize, nth_call) })
    })
}

#[no_mangle]
pub extern "C" fn ue4ss_asm_resolve_jmp_target_or_self(
    function_ptr: *mut c_void,
    max_bytes: u64,
) -> *mut c_void {
    ffi_ptr(|| {
        Some(unsafe { resolve_jmp_target_or_self_internal(function_ptr, max_bytes as usize) })
    })
}

#[no_mangle]
pub extern "C" fn ue4ss_detour_create(
    target: u64,
    callback: u64,
    user_trampoline: *mut u64,
) -> *mut Ue4ssHookDetourHandle {
    ffi_ptr(|| {
        let inner = unsafe { DetourHandle::new(target, callback, user_trampoline)? };
        Some(Box::into_raw(Box::new(Ue4ssHookDetourHandle { inner })))
    })
}

#[no_mangle]
pub extern "C" fn ue4ss_detour_destroy(handle: *mut Ue4ssHookDetourHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !handle.is_null() {
            drop(Box::from_raw(handle));
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ss_detour_hook(handle: *mut Ue4ssHookDetourHandle) -> bool {
    ffi_bool(|| unsafe { !handle.is_null() && (*handle).inner.hook() })
}

#[no_mangle]
pub extern "C" fn ue4ss_detour_unhook(handle: *mut Ue4ssHookDetourHandle) -> bool {
    ffi_bool(|| unsafe { !handle.is_null() && (*handle).inner.unhook() })
}

#[no_mangle]
pub extern "C" fn ue4ss_detour_is_hooked(handle: *const Ue4ssHookDetourHandle) -> bool {
    ffi_bool(|| unsafe { !handle.is_null() && (*handle).inner.hooked })
}

#[no_mangle]
pub extern "C" fn ue4ss_detour_set_scheme(handle: *mut Ue4ssHookDetourHandle, scheme: u32) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !handle.is_null() {
            (*handle).inner.scheme = scheme;
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ss_iat_hook_create(
    dll_name: *const c_char,
    api_name: *const c_char,
    callback: u64,
    user_original: *mut u64,
    module_name: *const u16,
) -> *mut Ue4ssHookIatHookHandle {
    ffi_ptr(|| {
        let inner = unsafe {
            IatHookHandle::new(dll_name, api_name, callback, user_original, module_name)?
        };
        Some(Box::into_raw(Box::new(Ue4ssHookIatHookHandle { inner })))
    })
}

#[no_mangle]
pub extern "C" fn ue4ss_iat_hook_destroy(handle: *mut Ue4ssHookIatHookHandle) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !handle.is_null() {
            drop(Box::from_raw(handle));
        }
    }));
}

#[no_mangle]
pub extern "C" fn ue4ss_iat_hook_hook(handle: *mut Ue4ssHookIatHookHandle) -> bool {
    ffi_bool(|| unsafe { !handle.is_null() && (*handle).inner.hook() })
}

#[no_mangle]
pub extern "C" fn ue4ss_iat_hook_unhook(handle: *mut Ue4ssHookIatHookHandle) -> bool {
    ffi_bool(|| unsafe { !handle.is_null() && (*handle).inner.unhook() })
}

#[no_mangle]
pub extern "C" fn ue4ss_iat_hook_is_hooked(handle: *const Ue4ssHookIatHookHandle) -> bool {
    ffi_bool(|| unsafe { !handle.is_null() && (*handle).inner.hooked })
}

#[no_mangle]
pub extern "C" fn ue4ss_helper_check_readable(handle: *mut c_void, src_ptr: *mut c_void) -> bool {
    ffi_bool(|| unsafe {
        if handle.is_null() || src_ptr.is_null() {
            return false;
        }

        let process = *(handle as *const Handle);
        let mut buffer = 0usize;
        let mut bytes_read = 0usize;
        ReadProcessMemory(
            process,
            src_ptr.cast(),
            (&mut buffer as *mut usize).cast(),
            std::mem::size_of::<usize>(),
            &mut bytes_read,
        ) != FALSE
    })
}

#[inline(never)]
pub fn force_link_exports() {
    let _ = ue4ss_asm_resolve_jmp as *const () as usize;
    let _ = ue4ss_asm_resolve_call as *const () as usize;
    let _ = ue4ss_asm_resolve_function_address_from_potential_jmp as *const () as usize;
    let _ = ue4ss_asm_find_nth_call_target as *const () as usize;
    let _ = ue4ss_asm_resolve_jmp_target_or_self as *const () as usize;
    let _ = ue4ss_detour_create as *const () as usize;
    let _ = ue4ss_detour_destroy as *const () as usize;
    let _ = ue4ss_detour_hook as *const () as usize;
    let _ = ue4ss_detour_unhook as *const () as usize;
    let _ = ue4ss_detour_is_hooked as *const () as usize;
    let _ = ue4ss_detour_set_scheme as *const () as usize;
    let _ = ue4ss_iat_hook_create as *const () as usize;
    let _ = ue4ss_iat_hook_destroy as *const () as usize;
    let _ = ue4ss_iat_hook_hook as *const () as usize;
    let _ = ue4ss_iat_hook_unhook as *const () as usize;
    let _ = ue4ss_iat_hook_is_hooked as *const () as usize;
    let _ = ue4ss_helper_check_readable as *const () as usize;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn absolute_jump_bytes_encode_mov_rax_jmp_rax() {
        let destination = 0x1122_3344_5566_7788u64;
        let bytes = absolute_jump_bytes(destination);

        assert_eq!(bytes[0], 0x48);
        assert_eq!(bytes[1], 0xB8);
        assert_eq!(
            u64::from_le_bytes(bytes[2..10].try_into().unwrap()),
            destination
        );
        assert_eq!(bytes[10], 0xFF);
        assert_eq!(bytes[11], 0xE0);
    }

    #[test]
    fn decode_overwritten_instructions_collects_full_patch_window() {
        let bytes = [0x53u8, 0x48, 0x83, 0xEC, 0x20, 0xC3];

        let (instructions, original, patch_size) =
            unsafe { decode_overwritten_instructions(bytes.as_ptr(), 5) }.unwrap();

        assert_eq!(instructions.len(), 2);
        assert_eq!(patch_size, 5);
        assert_eq!(original, bytes[..5]);
    }

    #[test]
    fn resolve_jmp_target_or_self_internal_resolves_near_jump() {
        let bytes = [0xE9u8, 0x01, 0x00, 0x00, 0x00, 0xCC, 0xC3];
        let expected = unsafe { bytes.as_ptr().add(6) as *mut c_void };

        let resolved = unsafe {
            resolve_jmp_target_or_self_internal(bytes.as_ptr() as *mut c_void, bytes.len())
        };

        assert_eq!(resolved, expected);
    }

    #[test]
    fn resolve_jmp_target_or_self_internal_returns_self_for_non_jump() {
        let bytes = [0xC3u8];
        let function_ptr = bytes.as_ptr() as *mut c_void;

        let resolved = unsafe { resolve_jmp_target_or_self_internal(function_ptr, bytes.len()) };

        assert_eq!(resolved, function_ptr);
    }

    #[test]
    fn find_nth_call_target_internal_returns_expected_call_target() {
        let bytes = [0xE8u8, 0x01, 0x00, 0x00, 0x00, 0xCC, 0xC3];
        let expected = unsafe { bytes.as_ptr().add(6) as *mut c_void };

        let resolved =
            unsafe { find_nth_call_target_internal(bytes.as_ptr() as *mut c_void, bytes.len(), 1) };

        assert_eq!(resolved, expected);
    }

    #[test]
    fn resolve_function_address_from_potential_jmp_ignores_call_prologue() {
        let bytes = [0xE8u8, 0x01, 0x00, 0x00, 0x00, 0xCC, 0xC3];
        let function_ptr = bytes.as_ptr() as *mut c_void;

        let resolved = unsafe { resolve_jmp_chain(function_ptr, 0) };

        assert_eq!(resolved, function_ptr);
    }

    #[test]
    fn find_nth_call_target_only_follows_jump_thunks() {
        let mut bytes = vec![0xCCu8; 64];
        let base = bytes.as_ptr() as usize;

        let main = 0usize;
        let thunk = 16usize;
        let final_target = 32usize;

        bytes[main] = 0xE8;
        let rel_call = (base + thunk) as isize - (base + main + 5) as isize;
        bytes[main + 1..main + 5].copy_from_slice(&(rel_call as i32).to_le_bytes());

        bytes[thunk] = 0xE9;
        let rel_jmp = (base + final_target) as isize - (base + thunk + 5) as isize;
        bytes[thunk + 1..thunk + 5].copy_from_slice(&(rel_jmp as i32).to_le_bytes());

        bytes[final_target] = 0xC3;

        let resolved = unsafe {
            find_nth_call_target_internal(bytes.as_ptr().add(main) as *mut c_void, bytes.len(), 1)
        };

        assert_eq!(resolved, unsafe {
            bytes.as_ptr().add(final_target) as *mut c_void
        });
    }

    #[test]
    fn find_nth_call_target_does_not_follow_nested_call_thunks() {
        let mut bytes = vec![0xCCu8; 64];
        let base = bytes.as_ptr() as usize;

        let main = 0usize;
        let wrapper = 16usize;
        let nested_call_target = 32usize;

        bytes[main] = 0xE8;
        let rel_main_call = (base + wrapper) as isize - (base + main + 5) as isize;
        bytes[main + 1..main + 5].copy_from_slice(&(rel_main_call as i32).to_le_bytes());

        bytes[wrapper] = 0xE8;
        let rel_wrapper_call = (base + nested_call_target) as isize - (base + wrapper + 5) as isize;
        bytes[wrapper + 1..wrapper + 5].copy_from_slice(&(rel_wrapper_call as i32).to_le_bytes());
        bytes[wrapper + 5] = 0xC3;

        bytes[nested_call_target] = 0xC3;

        let resolved = unsafe {
            find_nth_call_target_internal(bytes.as_ptr().add(main) as *mut c_void, bytes.len(), 1)
        };

        assert_eq!(resolved, unsafe {
            bytes.as_ptr().add(wrapper) as *mut c_void
        });
    }
}
