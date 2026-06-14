#pragma once

#ifndef UE4SSL_GENERATED_RUSTCORE_ABI_HPP
#define UE4SSL_GENERATED_RUSTCORE_ABI_HPP

#include <cstddef>
#include <cstdint>

namespace RC::Compat::RustCore
{
    struct SliceU16
    {
        const uint16_t* data{};
        size_t len{};
    };

    struct OwnedString
    {
        uint16_t* data{};
        size_t len{};
    };

    struct PathSnapshot
    {
        OwnedString root_directory{};
        OwnedString working_directory{};
        OwnedString mods_directory{};
        OwnedString game_executable_directory{};
        OwnedString settings_path_and_file{};
        OwnedString legacy_root_directory{};
        OwnedString object_dumper_output_directory{};
        OwnedString log_directory{};
        OwnedString game_path_and_exe_name{};
        uint8_t has_game_specific_config{};
    };

    struct DiscoveredMod
    {
        OwnedString mod_name{};
        OwnedString mod_path{};
        OwnedString dll_name{};
        uint8_t has_custom_dll_name{};
        uint8_t is_builtin{};
    };

    struct ModDiscovery
    {
        DiscoveredMod* mods{};
        size_t len{};
    };

    struct ProgramFlags
    {
        uint8_t is_program_started{};
        uint8_t processing_events{};
        uint8_t pause_events_processing{};
    };

    struct CppModHandle;

    struct CppModRuntimeStatus
    {
        uint8_t installable{};
        uint8_t installed{};
        uint8_t started{};
        uint8_t updates_disabled{};
        uint32_t failure_code{};
        uint32_t last_error{};
    };

    struct RustModStartContext
    {
        SliceU16 mod_path{};
    };

    struct HostVector
    {
        double x{};
        double y{};
        double z{};
    };

    struct HostHookHandle
    {
        void* function{};
        int32_t pre_id{};
        int32_t post_id{};
    };

    struct HostHookContext
    {
        void* context{};
    };

    static_assert(sizeof(SliceU16) == 16, "SliceU16 size mismatch");
    static_assert(alignof(SliceU16) == 8, "SliceU16 align mismatch");
    static_assert(offsetof(SliceU16, data) == 0, "SliceU16.data offset mismatch");
    static_assert(offsetof(SliceU16, len) == 8, "SliceU16.len offset mismatch");

    static_assert(sizeof(OwnedString) == 16, "OwnedString size mismatch");
    static_assert(alignof(OwnedString) == 8, "OwnedString align mismatch");
    static_assert(offsetof(OwnedString, data) == 0, "OwnedString.data offset mismatch");
    static_assert(offsetof(OwnedString, len) == 8, "OwnedString.len offset mismatch");

    static_assert(sizeof(PathSnapshot) == 152, "PathSnapshot size mismatch");
    static_assert(alignof(PathSnapshot) == 8, "PathSnapshot align mismatch");
    static_assert(offsetof(PathSnapshot, root_directory) == 0, "PathSnapshot.root_directory offset mismatch");
    static_assert(offsetof(PathSnapshot, working_directory) == 16, "PathSnapshot.working_directory offset mismatch");
    static_assert(offsetof(PathSnapshot, mods_directory) == 32, "PathSnapshot.mods_directory offset mismatch");
    static_assert(offsetof(PathSnapshot, game_executable_directory) == 48, "PathSnapshot.game_executable_directory offset mismatch");
    static_assert(offsetof(PathSnapshot, settings_path_and_file) == 64, "PathSnapshot.settings_path_and_file offset mismatch");
    static_assert(offsetof(PathSnapshot, legacy_root_directory) == 80, "PathSnapshot.legacy_root_directory offset mismatch");
    static_assert(offsetof(PathSnapshot, object_dumper_output_directory) == 96, "PathSnapshot.object_dumper_output_directory offset mismatch");
    static_assert(offsetof(PathSnapshot, log_directory) == 112, "PathSnapshot.log_directory offset mismatch");
    static_assert(offsetof(PathSnapshot, game_path_and_exe_name) == 128, "PathSnapshot.game_path_and_exe_name offset mismatch");
    static_assert(offsetof(PathSnapshot, has_game_specific_config) == 144, "PathSnapshot.has_game_specific_config offset mismatch");

    static_assert(sizeof(DiscoveredMod) == 56, "DiscoveredMod size mismatch");
    static_assert(alignof(DiscoveredMod) == 8, "DiscoveredMod align mismatch");
    static_assert(offsetof(DiscoveredMod, mod_name) == 0, "DiscoveredMod.mod_name offset mismatch");
    static_assert(offsetof(DiscoveredMod, mod_path) == 16, "DiscoveredMod.mod_path offset mismatch");
    static_assert(offsetof(DiscoveredMod, dll_name) == 32, "DiscoveredMod.dll_name offset mismatch");
    static_assert(offsetof(DiscoveredMod, has_custom_dll_name) == 48, "DiscoveredMod.has_custom_dll_name offset mismatch");
    static_assert(offsetof(DiscoveredMod, is_builtin) == 49, "DiscoveredMod.is_builtin offset mismatch");

    static_assert(sizeof(ModDiscovery) == 16, "ModDiscovery size mismatch");
    static_assert(alignof(ModDiscovery) == 8, "ModDiscovery align mismatch");
    static_assert(offsetof(ModDiscovery, mods) == 0, "ModDiscovery.mods offset mismatch");
    static_assert(offsetof(ModDiscovery, len) == 8, "ModDiscovery.len offset mismatch");

    static_assert(sizeof(ProgramFlags) == 3, "ProgramFlags size mismatch");
    static_assert(alignof(ProgramFlags) == 1, "ProgramFlags align mismatch");
    static_assert(offsetof(ProgramFlags, is_program_started) == 0, "ProgramFlags.is_program_started offset mismatch");
    static_assert(offsetof(ProgramFlags, processing_events) == 1, "ProgramFlags.processing_events offset mismatch");
    static_assert(offsetof(ProgramFlags, pause_events_processing) == 2, "ProgramFlags.pause_events_processing offset mismatch");

    static_assert(sizeof(CppModRuntimeStatus) == 12, "CppModRuntimeStatus size mismatch");
    static_assert(alignof(CppModRuntimeStatus) == 4, "CppModRuntimeStatus align mismatch");
    static_assert(offsetof(CppModRuntimeStatus, installable) == 0, "CppModRuntimeStatus.installable offset mismatch");
    static_assert(offsetof(CppModRuntimeStatus, installed) == 1, "CppModRuntimeStatus.installed offset mismatch");
    static_assert(offsetof(CppModRuntimeStatus, started) == 2, "CppModRuntimeStatus.started offset mismatch");
    static_assert(offsetof(CppModRuntimeStatus, updates_disabled) == 3, "CppModRuntimeStatus.updates_disabled offset mismatch");
    static_assert(offsetof(CppModRuntimeStatus, failure_code) == 4, "CppModRuntimeStatus.failure_code offset mismatch");
    static_assert(offsetof(CppModRuntimeStatus, last_error) == 8, "CppModRuntimeStatus.last_error offset mismatch");

    static_assert(sizeof(RustModStartContext) == 16, "RustModStartContext size mismatch");
    static_assert(alignof(RustModStartContext) == 8, "RustModStartContext align mismatch");
    static_assert(offsetof(RustModStartContext, mod_path) == 0, "RustModStartContext.mod_path offset mismatch");

    static_assert(sizeof(HostVector) == 24, "HostVector size mismatch");
    static_assert(alignof(HostVector) == 8, "HostVector align mismatch");
    static_assert(offsetof(HostVector, x) == 0, "HostVector.x offset mismatch");
    static_assert(offsetof(HostVector, y) == 8, "HostVector.y offset mismatch");
    static_assert(offsetof(HostVector, z) == 16, "HostVector.z offset mismatch");

    static_assert(sizeof(HostHookHandle) == 16, "HostHookHandle size mismatch");
    static_assert(alignof(HostHookHandle) == 8, "HostHookHandle align mismatch");
    static_assert(offsetof(HostHookHandle, function) == 0, "HostHookHandle.function offset mismatch");
    static_assert(offsetof(HostHookHandle, pre_id) == 8, "HostHookHandle.pre_id offset mismatch");
    static_assert(offsetof(HostHookHandle, post_id) == 12, "HostHookHandle.post_id offset mismatch");

    static_assert(sizeof(HostHookContext) == 8, "HostHookContext size mismatch");
    static_assert(alignof(HostHookContext) == 8, "HostHookContext align mismatch");
    static_assert(offsetof(HostHookContext, context) == 0, "HostHookContext.context offset mismatch");

}

#endif // UE4SSL_GENERATED_RUSTCORE_ABI_HPP
