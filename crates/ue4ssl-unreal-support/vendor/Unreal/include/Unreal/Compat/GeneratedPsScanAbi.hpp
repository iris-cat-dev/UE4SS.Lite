#pragma once

#ifndef UE4SSL_GENERATED_SCAN_ABI_HPP
#define UE4SSL_GENERATED_SCAN_ABI_HPP

#include <cstddef>
#include <cstdint>

namespace RC::Compat::Scan
{
    using LogFn = void (*)(const uint16_t* msg);

    struct PsEngineVersion
    {
        uint16_t major{};
        uint16_t minor{};
    };

    struct PsScanConfig
    {
        uint8_t guobject_array{};
        uint8_t fname_tostring{};
        uint8_t fname_ctor_wchar{};
        uint8_t gmalloc{};
        uint8_t static_construct_object_internal{};
        uint8_t ftext_fstring{};
        uint8_t engine_version{};
        uint8_t ufunction_bind{};
        uint8_t fuobject_hash_tables_get{};
        uint8_t gnatives{};
        uint8_t console_manager_singleton{};
        uint8_t gameengine_tick{};
        uint8_t static_find_object_fast{};
    };

    struct PsCtx
    {
        LogFn default_fn{};
        LogFn normal_fn{};
        LogFn verbose_fn{};
        LogFn warning_fn{};
        LogFn error_fn{};
        PsScanConfig config{};
    };

    struct PsScanResults
    {
        size_t guobject_array{};
        size_t fname_tostring{};
        size_t fname_ctor_wchar{};
        size_t gmalloc{};
        size_t static_construct_object_internal{};
        size_t ftext_fstring{};
        PsEngineVersion engine_version{};
        size_t ufunction_bind{};
        size_t fuobject_hash_tables_get{};
        size_t gnatives{};
        size_t console_manager_singleton{};
        size_t gameengine_tick{};
        size_t static_find_object_fast{};
    };

    static_assert(sizeof(PsEngineVersion) == 4, "PsEngineVersion size mismatch");
    static_assert(alignof(PsEngineVersion) == 2, "PsEngineVersion align mismatch");
    static_assert(offsetof(PsEngineVersion, major) == 0, "PsEngineVersion.major offset mismatch");
    static_assert(offsetof(PsEngineVersion, minor) == 2, "PsEngineVersion.minor offset mismatch");

    static_assert(sizeof(PsScanConfig) == 13, "PsScanConfig size mismatch");
    static_assert(alignof(PsScanConfig) == 1, "PsScanConfig align mismatch");
    static_assert(offsetof(PsScanConfig, guobject_array) == 0, "PsScanConfig.guobject_array offset mismatch");
    static_assert(offsetof(PsScanConfig, fname_tostring) == 1, "PsScanConfig.fname_tostring offset mismatch");
    static_assert(offsetof(PsScanConfig, fname_ctor_wchar) == 2, "PsScanConfig.fname_ctor_wchar offset mismatch");
    static_assert(offsetof(PsScanConfig, gmalloc) == 3, "PsScanConfig.gmalloc offset mismatch");
    static_assert(offsetof(PsScanConfig, static_construct_object_internal) == 4, "PsScanConfig.static_construct_object_internal offset mismatch");
    static_assert(offsetof(PsScanConfig, ftext_fstring) == 5, "PsScanConfig.ftext_fstring offset mismatch");
    static_assert(offsetof(PsScanConfig, engine_version) == 6, "PsScanConfig.engine_version offset mismatch");
    static_assert(offsetof(PsScanConfig, ufunction_bind) == 7, "PsScanConfig.ufunction_bind offset mismatch");
    static_assert(offsetof(PsScanConfig, fuobject_hash_tables_get) == 8, "PsScanConfig.fuobject_hash_tables_get offset mismatch");
    static_assert(offsetof(PsScanConfig, gnatives) == 9, "PsScanConfig.gnatives offset mismatch");
    static_assert(offsetof(PsScanConfig, console_manager_singleton) == 10, "PsScanConfig.console_manager_singleton offset mismatch");
    static_assert(offsetof(PsScanConfig, gameengine_tick) == 11, "PsScanConfig.gameengine_tick offset mismatch");
    static_assert(offsetof(PsScanConfig, static_find_object_fast) == 12, "PsScanConfig.static_find_object_fast offset mismatch");

    static_assert(sizeof(PsCtx) == 56, "PsCtx size mismatch");
    static_assert(alignof(PsCtx) == 8, "PsCtx align mismatch");
    static_assert(offsetof(PsCtx, default_fn) == 0, "PsCtx.default_fn offset mismatch");
    static_assert(offsetof(PsCtx, normal_fn) == 8, "PsCtx.normal_fn offset mismatch");
    static_assert(offsetof(PsCtx, verbose_fn) == 16, "PsCtx.verbose_fn offset mismatch");
    static_assert(offsetof(PsCtx, warning_fn) == 24, "PsCtx.warning_fn offset mismatch");
    static_assert(offsetof(PsCtx, error_fn) == 32, "PsCtx.error_fn offset mismatch");
    static_assert(offsetof(PsCtx, config) == 40, "PsCtx.config offset mismatch");

    static_assert(sizeof(PsScanResults) == 104, "PsScanResults size mismatch");
    static_assert(alignof(PsScanResults) == 8, "PsScanResults align mismatch");
    static_assert(offsetof(PsScanResults, guobject_array) == 0, "PsScanResults.guobject_array offset mismatch");
    static_assert(offsetof(PsScanResults, fname_tostring) == 8, "PsScanResults.fname_tostring offset mismatch");
    static_assert(offsetof(PsScanResults, fname_ctor_wchar) == 16, "PsScanResults.fname_ctor_wchar offset mismatch");
    static_assert(offsetof(PsScanResults, gmalloc) == 24, "PsScanResults.gmalloc offset mismatch");
    static_assert(offsetof(PsScanResults, static_construct_object_internal) == 32, "PsScanResults.static_construct_object_internal offset mismatch");
    static_assert(offsetof(PsScanResults, ftext_fstring) == 40, "PsScanResults.ftext_fstring offset mismatch");
    static_assert(offsetof(PsScanResults, engine_version) == 48, "PsScanResults.engine_version offset mismatch");
    static_assert(offsetof(PsScanResults, ufunction_bind) == 56, "PsScanResults.ufunction_bind offset mismatch");
    static_assert(offsetof(PsScanResults, fuobject_hash_tables_get) == 64, "PsScanResults.fuobject_hash_tables_get offset mismatch");
    static_assert(offsetof(PsScanResults, gnatives) == 72, "PsScanResults.gnatives offset mismatch");
    static_assert(offsetof(PsScanResults, console_manager_singleton) == 80, "PsScanResults.console_manager_singleton offset mismatch");
    static_assert(offsetof(PsScanResults, gameengine_tick) == 88, "PsScanResults.gameengine_tick offset mismatch");
    static_assert(offsetof(PsScanResults, static_find_object_fast) == 96, "PsScanResults.static_find_object_fast offset mismatch");

}

#endif // UE4SSL_GENERATED_SCAN_ABI_HPP
