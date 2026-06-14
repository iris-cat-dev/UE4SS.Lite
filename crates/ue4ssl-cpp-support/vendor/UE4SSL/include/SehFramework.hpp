#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <string>
#include <functional>
#include <type_traits>

#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Common.hpp>

namespace RC::Unreal
{
    class UObject;
    class UFunction;
    class FProperty;
    class FString;
}

namespace RC::Seh
{
    // =========================================================================
    // Core SEH filter
    // =========================================================================

    RC_UE4SS_API LONG WINAPI FilterAndLog(const wchar_t* subsystem, const wchar_t* context,
                             DWORD code, EXCEPTION_POINTERS* ep);

    RC_UE4SS_API LONG WINAPI FilterAndLog(const wchar_t* context, DWORD code, EXCEPTION_POINTERS* ep);

    RC_UE4SS_API DWORD GetLastSehCode();

    // =========================================================================
    // Structured stability error logging
    // =========================================================================

    RC_UE4SS_API void LogStabilityError(const wchar_t* error_code,
                           const wchar_t* subsystem,
                           const wchar_t* operation,
                           const std::wstring& message,
                           uint32_t seh_code = 0,
                           uintptr_t object_addr = 0,
                           const std::wstring& script_name = L"-");

    // =========================================================================
    // Memory probes
    // =========================================================================

    RC_UE4SS_API bool ProbeReadable(const void* data, size_t size);

    RC_UE4SS_API bool ProbeWrite(void* addr, size_t size);

    // =========================================================================
    // UObject validity
    // =========================================================================

    RC_UE4SS_API bool IsValidObject(Unreal::UObject* object);

    // =========================================================================
    // Safe UE API wrappers
    // =========================================================================

    RC_UE4SS_API bool SafeProcessEvent(Unreal::UObject* object, Unreal::UFunction* function, void* params);

    RC_UE4SS_API Unreal::UObject*   SafeFindFirstOf(const wchar_t* name);
    RC_UE4SS_API Unreal::UObject*   SafeStaticFindObject(const std::wstring& path);
    RC_UE4SS_API Unreal::UFunction* SafeGetFunctionByName(Unreal::UObject* object, const wchar_t* name);

    RC_UE4SS_API bool SafeExtractFString(const Unreal::FString* fstr, const wchar_t** out_data, int32_t* out_len);
    RC_UE4SS_API bool SafePropertyValuePtr(Unreal::FProperty* prop, void* container, void** out_value);

    // =========================================================================
    // SEH-protected callable invocation (for Detour callbacks)
    // =========================================================================

    RC_UE4SS_API bool InvokeProtectedRaw(void(*fn)(void*), void* ctx, const wchar_t* context);

    // High-level: wraps any callable (lambda, etc.) in SEH protection.
    // The callable is type-erased through a non-capturing lambda -> function pointer.
    template<typename Fn>
    bool InvokeProtected(Fn&& fn, const wchar_t* context)
    {
        auto* ptr = &fn;
        return InvokeProtectedRaw(
            +[](void* p) { (*static_cast<std::remove_reference_t<Fn>*>(p))(); },
            static_cast<void*>(ptr), context);
    }

} // namespace RC::Seh
