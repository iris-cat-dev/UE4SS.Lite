#pragma once

#include <cstddef>
#include <cstdint>

extern "C"
{
    auto ue4ssl_native_cppmod_call_start(void* fn) -> void*;
    auto ue4ssl_native_cppmod_call_uninstall(void* fn, void* mod) -> void;
    auto ue4ssl_native_cppmod_call_on_unreal_init(void* mod) -> void;
    auto ue4ssl_native_cppmod_call_on_ui_init(void* mod) -> void;
    auto ue4ssl_native_cppmod_call_on_program_start(void* mod) -> void;
    auto ue4ssl_native_cppmod_call_on_update(void* mod) -> void;
    auto ue4ssl_native_cppmod_call_on_dll_load(void* mod, const uint16_t* dll_name, size_t dll_name_len) -> void;
    auto ue4ssl_native_cppmod_free_library(void* module_handle) -> void;
}
