#pragma once

#ifndef UE4SSL_GENERATED_HOST_ABI_HPP
#define UE4SSL_GENERATED_HOST_ABI_HPP

#include <cstddef>
#include <cstdint>

namespace RC::Compat::Host
{
    struct ReinstallPlan
    {
        uint8_t should_pause_processing{};
        uint8_t should_resume_before_restart{};
        uint8_t should_fire_unreal_init{};
        uint8_t should_fire_program_start{};
    };

    enum class ReinstallAction : uint32_t
    {
        ResetDllDispatchCache = 1,
        SetPauseProcessing = 2,
        UninstallMods = 3,
        RemoveScriptKeybinds = 4,
        SetupMods = 5,
        StartCppMods = 6,
        FireUnrealInit = 7,
        FireProgramStart = 8,
    };

    struct ReinstallStep
    {
        uint32_t action{};
        uint8_t value{};
    };

    constexpr size_t ReinstallSequenceCapacity = 16;

    struct ReinstallSequence
    {
        ReinstallStep steps[ReinstallSequenceCapacity]{};
        size_t len{};
    };

    enum class ModStartupAction : uint32_t
    {
        StartNamedMod = 1,
        StartDiscoveredMods = 2,
    };

    struct ModStartupStep
    {
        uint32_t action{};
        const uint16_t* mod_name{};
        size_t mod_name_len{};
    };

    constexpr size_t ModStartupSequenceCapacity = 8;

    struct ModStartupSequence
    {
        ModStartupStep steps[ModStartupSequenceCapacity]{};
        size_t len{};
    };

    struct UnrealConfigPlan
    {
        uint32_t num_scan_threads{};
        uint32_t multithreading_module_size_threshold{};
        uint32_t engine_version_major{};
        uint32_t engine_version_minor{};
        uint8_t has_num_scan_threads{};
        uint8_t has_multithreading_module_size_threshold{};
        uint8_t has_engine_version_override{};
        uint8_t engine_version_override_invalid{};
        uint8_t should_enable_builtin_guobjectarray_fallback{};
        uint8_t fexec_vtable_offset_in_local_player{};
        uint8_t is_fexec_vtable_offset_in_local_player_in_range{};
    };

    static_assert(sizeof(ReinstallPlan) == 4, "ReinstallPlan size mismatch");
    static_assert(alignof(ReinstallPlan) == 1, "ReinstallPlan align mismatch");
    static_assert(offsetof(ReinstallPlan, should_pause_processing) == 0, "ReinstallPlan.should_pause_processing offset mismatch");
    static_assert(offsetof(ReinstallPlan, should_resume_before_restart) == 1, "ReinstallPlan.should_resume_before_restart offset mismatch");
    static_assert(offsetof(ReinstallPlan, should_fire_unreal_init) == 2, "ReinstallPlan.should_fire_unreal_init offset mismatch");
    static_assert(offsetof(ReinstallPlan, should_fire_program_start) == 3, "ReinstallPlan.should_fire_program_start offset mismatch");

    static_assert(sizeof(ReinstallStep) == 8, "ReinstallStep size mismatch");
    static_assert(alignof(ReinstallStep) == 4, "ReinstallStep align mismatch");
    static_assert(offsetof(ReinstallStep, action) == 0, "ReinstallStep.action offset mismatch");
    static_assert(offsetof(ReinstallStep, value) == 4, "ReinstallStep.value offset mismatch");

    static_assert(sizeof(ReinstallSequence) == 136, "ReinstallSequence size mismatch");
    static_assert(alignof(ReinstallSequence) == 8, "ReinstallSequence align mismatch");
    static_assert(offsetof(ReinstallSequence, steps) == 0, "ReinstallSequence.steps offset mismatch");
    static_assert(offsetof(ReinstallSequence, len) == 128, "ReinstallSequence.len offset mismatch");

    static_assert(sizeof(ModStartupStep) == 24, "ModStartupStep size mismatch");
    static_assert(alignof(ModStartupStep) == 8, "ModStartupStep align mismatch");
    static_assert(offsetof(ModStartupStep, action) == 0, "ModStartupStep.action offset mismatch");
    static_assert(offsetof(ModStartupStep, mod_name) == 8, "ModStartupStep.mod_name offset mismatch");
    static_assert(offsetof(ModStartupStep, mod_name_len) == 16, "ModStartupStep.mod_name_len offset mismatch");

    static_assert(sizeof(ModStartupSequence) == 200, "ModStartupSequence size mismatch");
    static_assert(alignof(ModStartupSequence) == 8, "ModStartupSequence align mismatch");
    static_assert(offsetof(ModStartupSequence, steps) == 0, "ModStartupSequence.steps offset mismatch");
    static_assert(offsetof(ModStartupSequence, len) == 192, "ModStartupSequence.len offset mismatch");

    static_assert(sizeof(UnrealConfigPlan) == 24, "UnrealConfigPlan size mismatch");
    static_assert(alignof(UnrealConfigPlan) == 4, "UnrealConfigPlan align mismatch");
    static_assert(offsetof(UnrealConfigPlan, num_scan_threads) == 0, "UnrealConfigPlan.num_scan_threads offset mismatch");
    static_assert(offsetof(UnrealConfigPlan, multithreading_module_size_threshold) == 4, "UnrealConfigPlan.multithreading_module_size_threshold offset mismatch");
    static_assert(offsetof(UnrealConfigPlan, engine_version_major) == 8, "UnrealConfigPlan.engine_version_major offset mismatch");
    static_assert(offsetof(UnrealConfigPlan, engine_version_minor) == 12, "UnrealConfigPlan.engine_version_minor offset mismatch");
    static_assert(offsetof(UnrealConfigPlan, has_num_scan_threads) == 16, "UnrealConfigPlan.has_num_scan_threads offset mismatch");
    static_assert(offsetof(UnrealConfigPlan, has_multithreading_module_size_threshold) == 17, "UnrealConfigPlan.has_multithreading_module_size_threshold offset mismatch");
    static_assert(offsetof(UnrealConfigPlan, has_engine_version_override) == 18, "UnrealConfigPlan.has_engine_version_override offset mismatch");
    static_assert(offsetof(UnrealConfigPlan, engine_version_override_invalid) == 19, "UnrealConfigPlan.engine_version_override_invalid offset mismatch");
    static_assert(offsetof(UnrealConfigPlan, should_enable_builtin_guobjectarray_fallback) == 20, "UnrealConfigPlan.should_enable_builtin_guobjectarray_fallback offset mismatch");
    static_assert(offsetof(UnrealConfigPlan, fexec_vtable_offset_in_local_player) == 21, "UnrealConfigPlan.fexec_vtable_offset_in_local_player offset mismatch");
    static_assert(offsetof(UnrealConfigPlan, is_fexec_vtable_offset_in_local_player_in_range) == 22, "UnrealConfigPlan.is_fexec_vtable_offset_in_local_player_in_range offset mismatch");

}

#endif // UE4SSL_GENERATED_HOST_ABI_HPP
