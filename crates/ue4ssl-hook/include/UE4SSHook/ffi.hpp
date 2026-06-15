#pragma once

#include <cstdint>

extern "C"
{
    struct Ue4ssHookDetourHandle;
    struct Ue4ssHookIatHookHandle;

    auto ue4ss_asm_resolve_jmp(void* instruction_ptr) -> void*;
    auto ue4ss_asm_resolve_call(void* instruction_ptr) -> void*;
    auto ue4ss_asm_resolve_function_address_from_potential_jmp(void* function_ptr) -> void*;
    auto ue4ss_asm_find_nth_call_target(void* function_ptr, uint64_t max_bytes, uint32_t nth_call) -> void*;
    auto ue4ss_asm_resolve_jmp_target_or_self(void* function_ptr, uint64_t max_bytes) -> void*;

    auto ue4ss_detour_create(uint64_t target, uint64_t callback, uint64_t* user_trampoline) -> Ue4ssHookDetourHandle*;
    auto ue4ss_detour_destroy(Ue4ssHookDetourHandle* handle) -> void;
    auto ue4ss_detour_hook(Ue4ssHookDetourHandle* handle) -> bool;
    auto ue4ss_detour_unhook(Ue4ssHookDetourHandle* handle) -> bool;
    auto ue4ss_detour_is_hooked(const Ue4ssHookDetourHandle* handle) -> bool;
    auto ue4ss_detour_set_scheme(Ue4ssHookDetourHandle* handle, uint32_t scheme) -> void;

    auto ue4ss_iat_hook_create(const char* dll_name,
                               const char* api_name,
                               uint64_t callback,
                               uint64_t* user_original,
                               const wchar_t* module_name) -> Ue4ssHookIatHookHandle*;
    auto ue4ss_iat_hook_destroy(Ue4ssHookIatHookHandle* handle) -> void;
    auto ue4ss_iat_hook_hook(Ue4ssHookIatHookHandle* handle) -> bool;
    auto ue4ss_iat_hook_unhook(Ue4ssHookIatHookHandle* handle) -> bool;
    auto ue4ss_iat_hook_is_hooked(const Ue4ssHookIatHookHandle* handle) -> bool;

    auto ue4ss_helper_check_readable(void* handle, void* src_ptr) -> bool;
}
