#define NOMINMAX

#include <SehFramework.hpp>
#include <thread>

#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

namespace RC::Seh
{
    // Thread-local storage for the last SEH exception code
    static thread_local DWORD tls_last_seh_code = 0;

    // =========================================================================
    // Core SEH filter
    // =========================================================================

    LONG WINAPI FilterAndLog(const wchar_t* subsystem, const wchar_t* context,
                             DWORD code, EXCEPTION_POINTERS* ep)
    {
        tls_last_seh_code = code;
        void* addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;

        if (!Output::has_internal_error())
        {
            Output::send<LogLevel::Error>(
                STR("[{}] SEH exception in {}: code=0x{:08X}, addr={}\n"),
                subsystem, context, code, addr);
        }
        else
        {
            printf_s("[%ls] SEH exception in %ls: code=0x%08lX addr=%p\n",
                     subsystem, context, code, addr);
        }

        return EXCEPTION_EXECUTE_HANDLER;
    }

    LONG WINAPI FilterAndLog(const wchar_t* context, DWORD code, EXCEPTION_POINTERS* ep)
    {
        return FilterAndLog(L"UE4SS", context, code, ep);
    }

    DWORD GetLastSehCode()
    {
        return tls_last_seh_code;
    }

    // =========================================================================
    // Structured stability error logging
    // =========================================================================

    void LogStabilityError(const wchar_t* error_code,
                           const wchar_t* subsystem,
                           const wchar_t* operation,
                           const std::wstring& message,
                           uint32_t seh_code,
                           uintptr_t object_addr,
                           const std::wstring& script_name)
    {
        auto thread_hash = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));

        if (!Output::has_internal_error())
        {
            Output::send<LogLevel::Error>(
                STR("[STABILITY] code={} subsystem={} op={} msg={} seh=0x{:08X} addr=0x{:X} script={} tid=0x{:X}\n"),
                error_code, subsystem, operation, message,
                seh_code, object_addr, script_name, thread_hash);
        }
        else
        {
            printf_s("[STABILITY] code=%ls subsystem=%ls op=%ls seh=0x%08X addr=0x%llX tid=0x%X\n",
                     error_code, subsystem, operation, seh_code,
                     static_cast<unsigned long long>(object_addr), thread_hash);
        }
    }

    // =========================================================================
    // Memory probes
    // =========================================================================

    static int probe_readable_inner(const void* data, size_t size)
    {
        __try
        {
            const auto* bytes = static_cast<const volatile uint8_t*>(data);
            volatile uint8_t head = bytes[0];
            (void)head;
            if (size > 1)
            {
                volatile uint8_t tail = bytes[size - 1];
                (void)tail;
            }
            return 1;
        }
        __except (FilterAndLog(L"UE4SS", L"ProbeReadable", GetExceptionCode(), GetExceptionInformation()))
        {
            return 0;
        }
    }

    bool ProbeReadable(const void* data, size_t size)
    {
        if (!data || size == 0) return false;
        return probe_readable_inner(data, size) != 0;
    }

    static int probe_write_inner(void* addr, size_t size)
    {
        __try
        {
            auto* bytes = static_cast<volatile uint8_t*>(addr);
            volatile uint8_t v = bytes[0];
            bytes[0] = v;
            if (size > 1)
            {
                volatile uint8_t tail = bytes[size - 1];
                bytes[size - 1] = tail;
            }
            return 1;
        }
        __except (FilterAndLog(L"UE4SS", L"ProbeWrite", GetExceptionCode(), GetExceptionInformation()))
        {
            return 0;
        }
    }

    bool ProbeWrite(void* addr, size_t size)
    {
        if (!addr || size == 0) return false;
        return probe_write_inner(addr, size) != 0;
    }

    // =========================================================================
    // UObject validity
    // =========================================================================

    static int is_valid_object_inner(Unreal::UObject* object)
    {
        __try
        {
            return Unreal::UObjectArray::IsValid(object->GetObjectItem(), false) ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool IsValidObject(Unreal::UObject* object)
    {
        if (!object) return false;
        return is_valid_object_inner(object) != 0;
    }

    // =========================================================================
    // Safe UE API wrappers
    // =========================================================================

    static int safe_process_event_inner(Unreal::UObject* object, Unreal::UFunction* function, void* params)
    {
        __try
        {
            object->ProcessEvent(function, params);
            return 1;
        }
        __except (FilterAndLog(L"UE4SS", L"ProcessEvent", GetExceptionCode(), GetExceptionInformation()))
        {
            return 0;
        }
    }

    bool SafeProcessEvent(Unreal::UObject* object, Unreal::UFunction* function, void* params)
    {
        if (!object || !function) return false;
        return safe_process_event_inner(object, function, params) != 0;
    }

    static Unreal::UObject* safe_find_first_instance_of_class_inner(const wchar_t* name)
    {
        __try
        {
            return Unreal::UObjectGlobals::FindFirstInstanceOfClass(name);
        }
        __except (FilterAndLog(L"UE4SS", L"FindFirstInstanceOfClass", GetExceptionCode(), GetExceptionInformation()))
        {
            return nullptr;
        }
    }

    Unreal::UObject* SafeFindFirstInstanceOfClass(const wchar_t* name)
    {
        if (!name) return nullptr;
        return safe_find_first_instance_of_class_inner(name);
    }

    Unreal::UObject* SafeFindFirstOf(const wchar_t* name)
    {
        return SafeFindFirstInstanceOfClass(name);
    }

    static Unreal::UObject* safe_static_find_object_inner(const std::wstring* path)
    {
        __try
        {
            return Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, *path);
        }
        __except (FilterAndLog(L"UE4SS", L"StaticFindObject", GetExceptionCode(), GetExceptionInformation()))
        {
            return nullptr;
        }
    }

    Unreal::UObject* SafeStaticFindObject(const std::wstring& path)
    {
        return safe_static_find_object_inner(&path);
    }

    static Unreal::UFunction* safe_get_function_by_name_inner(Unreal::UObject* object, const wchar_t* name)
    {
        __try
        {
            return object->GetFunctionByNameInChain(name);
        }
        __except (FilterAndLog(L"UE4SS", L"GetFunctionByName", GetExceptionCode(), GetExceptionInformation()))
        {
            return nullptr;
        }
    }

    Unreal::UFunction* SafeGetFunctionByName(Unreal::UObject* object, const wchar_t* name)
    {
        if (!object || !name) return nullptr;
        return safe_get_function_by_name_inner(object, name);
    }

    static int safe_extract_fstring_inner(const Unreal::FString* fstr, const wchar_t** out_data, int32_t* out_len)
    {
        __try
        {
            if (!fstr) return 1;
            const auto& chars = fstr->GetCharArray();
            int32_t num = chars.Num();
            if (num <= 0) return 1;

            const wchar_t* data = chars.GetData();
            if (!data) return 0;

            volatile wchar_t head = data[0];
            (void)head;
            volatile wchar_t tail = data[num - 1];
            (void)tail;

            *out_data = data;
            *out_len = (data[num - 1] == L'\0') ? (num - 1) : num;
            return 1;
        }
        __except (FilterAndLog(L"UE4SS", L"ExtractFString", GetExceptionCode(), GetExceptionInformation()))
        {
            *out_data = nullptr;
            *out_len = 0;
            return 0;
        }
    }

    bool SafeExtractFString(const Unreal::FString* fstr, const wchar_t** out_data, int32_t* out_len)
    {
        if (!out_data || !out_len) return false;
        *out_data = nullptr;
        *out_len = 0;
        return safe_extract_fstring_inner(fstr, out_data, out_len) != 0;
    }

    static int safe_property_value_ptr_inner(Unreal::FProperty* prop, void* container, void** out_value)
    {
        __try
        {
            *out_value = prop->ContainerPtrToValuePtr<void>(container);
            return 1;
        }
        __except (FilterAndLog(L"UE4SS", L"PropertyValuePtr", GetExceptionCode(), GetExceptionInformation()))
        {
            *out_value = nullptr;
            return 0;
        }
    }

    bool SafePropertyValuePtr(Unreal::FProperty* prop, void* container, void** out_value)
    {
        if (!out_value) return false;
        *out_value = nullptr;
        if (!prop || !container) return false;
        return safe_property_value_ptr_inner(prop, container, out_value) != 0;
    }

    // =========================================================================
    // SEH-protected callable invocation
    // =========================================================================

    static int invoke_protected_raw_inner(void(*fn)(void*), void* ctx, const wchar_t* context)
    {
        __try
        {
            fn(ctx);
            return 1;
        }
        __except (FilterAndLog(L"Detour", context, GetExceptionCode(), GetExceptionInformation()))
        {
            return 0;
        }
    }

    bool InvokeProtectedRaw(void(*fn)(void*), void* ctx, const wchar_t* context)
    {
        return invoke_protected_raw_inner(fn, ctx, context) != 0;
    }

} // namespace RC::Seh
