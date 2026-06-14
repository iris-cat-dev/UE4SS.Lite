#include "JSInternal.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <functional>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/CoreUObject/UObject/FAnsiStrProperty.hpp>
#include <Unreal/Core/Containers/FAnsiString.hpp>
#include <Unreal/Property/FTextProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/Property/NumericPropertyTypes.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>

namespace RC::JSScript
{
    // ============================================
    // Shared utilities
    // ============================================

    JSMod* get_js_mod(JSContext* ctx)
    {
        return static_cast<JSMod*>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
    }

    std::wstring utf8_to_wide(const std::string& utf8)
    {
        if (utf8.empty()) return {};
        int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        if (wide_len <= 0) return {};
        std::wstring out(static_cast<size_t>(wide_len), 0);
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &out[0], wide_len);
        return out;
    }

    std::string wide_to_utf8(const std::wstring& wide)
    {
        if (wide.empty()) return {};
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (utf8_len <= 0) return {};
        std::string out(static_cast<size_t>(utf8_len), 0);
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), &out[0], utf8_len, nullptr, nullptr);
        return out;
    }

    // ============================================
    // SEH helpers (QuickJS-specific, module-internal)
    // ============================================

    // Module-internal silent SEH filter: captures code for deferred logging
    static DWORD s_last_seh_code = 0;

    static LONG seh_filter(DWORD code)
    {
        s_last_seh_code = code;
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // log_stability_error is now an inline delegate in JSInternal.hpp -> Seh::LogStabilityError

    // Inner SEH wrappers are separated to satisfy MSVC C2712.
    static int seh_js_call_inner(JSContext* ctx, JSValueConst func, JSValueConst this_obj,
                                 int argc, JSValueConst* argv, JSValue* out_result)
    {
        s_last_seh_code = 0;
        __try
        {
            *out_result = JS_Call(ctx, func, this_obj, argc, argv);
            return 1;
        }
        __except (seh_filter(GetExceptionCode()))
        {
            return 0;
        }
    }

    static int seh_js_eval_inner(JSContext* ctx, const char* input, size_t input_len,
                                 const char* filename, int eval_flags, JSValue* out_result)
    {
        s_last_seh_code = 0;
        __try
        {
            *out_result = JS_Eval(ctx, input, input_len, filename, eval_flags);
            return 1;
        }
        __except (seh_filter(GetExceptionCode()))
        {
            return 0;
        }
    }

    static int seh_js_execute_pending_job_inner(JSRuntime* rt, JSContext** pctx)
    {
        s_last_seh_code = 0;
        __try
        {
            return JS_ExecutePendingJob(rt, pctx);
        }
        __except (seh_filter(GetExceptionCode()))
        {
            return -2;
        }
    }

    bool safe_js_call(JSContext* ctx, JSValueConst func, JSValueConst this_obj,
                      int argc, JSValueConst* argv, JSValue* out_result)
    {
        if (seh_js_call_inner(ctx, func, this_obj, argc, argv, out_result) != 0)
        {
            return true;
        }
        log_stability_error(L"E_JS_CALL_SEH", L"Runtime", L"JS_Call",
                            L"SEH exception caught in JS_Call", s_last_seh_code);
        return false;
    }

    bool safe_js_eval(JSContext* ctx, const char* input, size_t input_len,
                      const char* filename, int eval_flags, JSValue* out_result)
    {
        if (seh_js_eval_inner(ctx, input, input_len, filename, eval_flags, out_result) != 0)
        {
            return true;
        }
        log_stability_error(L"E_JS_EVAL_SEH", L"Runtime", L"JS_Eval",
                            L"SEH exception caught in JS_Eval", s_last_seh_code, 0,
                            filename ? utf8_to_wide(std::string(filename)) : std::wstring(L"-"));
        return false;
    }

    int safe_js_execute_pending_job(JSRuntime* rt, JSContext** pctx)
    {
        const int result = seh_js_execute_pending_job_inner(rt, pctx);
        if (result == -2)
        {
            log_stability_error(L"E_JS_PENDING_JOB_SEH", L"Runtime", L"JS_ExecutePendingJob",
                                L"SEH exception caught in JS_ExecutePendingJob", s_last_seh_code);
        }
        return result;
    }

    // ============================================
    // ParamRef: writable wrapper for UFunction OutParam parameters
    // ============================================

    struct ParamRefData
    {
        Unreal::FProperty* prop;
        void* data;
    };

    static JSClassID js_param_ref_class_id = 0;

    static void js_param_ref_finalizer(JSRuntime* rt, JSValue val)
    {
        auto* ref = static_cast<ParamRefData*>(JS_GetOpaque(val, js_param_ref_class_id));
        if (ref) js_free_rt(rt, ref);
    }

    static JSClassDef js_param_ref_class = {
        .class_name = "ParamRef",
        .finalizer = js_param_ref_finalizer,
        .gc_mark = nullptr,
        .call = nullptr,
        .exotic = nullptr,
    };

    static JSValue js_param_ref_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)argc; (void)argv;
        auto* ref = static_cast<ParamRefData*>(JS_GetOpaque(this_val, js_param_ref_class_id));
        if (!ref || !ref->prop || !ref->data) return JS_NULL;
        __try
        {
            return property_to_jsvalue(ctx, ref->prop, ref->data);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ref->data = nullptr;
            return JS_NULL;
        }
    }

    static JSValue js_param_ref_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1) return JS_ThrowTypeError(ctx, "ParamRef.set requires 1 argument");
        auto* ref = static_cast<ParamRefData*>(JS_GetOpaque(this_val, js_param_ref_class_id));
        if (!ref || !ref->prop || !ref->data)
            return JS_NewBool(ctx, false);
        __try
        {
            jsvalue_to_property(ctx, ref->prop, ref->data, argv[0]);
            return JS_NewBool(ctx, true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] ParamRef.set: memory access failed (stale pointer), marking invalid\n"));
            ref->data = nullptr;
            return JS_NewBool(ctx, false);
        }
    }

    static JSValue js_param_ref_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)argc; (void)argv;
        auto* ref = static_cast<ParamRefData*>(JS_GetOpaque(this_val, js_param_ref_class_id));
        if (!ref || !ref->prop || !ref->data) return JS_NewString(ctx, "[ParamRef invalid]");
        __try
        {
            JSValue v = property_to_jsvalue(ctx, ref->prop, ref->data);
            const char* s = JS_ToCString(ctx, v);
            JSValue result = JS_NewString(ctx, s ? s : "");
            if (s) JS_FreeCString(ctx, s);
            JS_FreeValue(ctx, v);
            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ref->data = nullptr;
            return JS_NewString(ctx, "[ParamRef invalid]");
        }
    }

    static JSValue js_param_ref_is_valid(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)argc; (void)argv;
        auto* ref = static_cast<ParamRefData*>(JS_GetOpaque(this_val, js_param_ref_class_id));
        return JS_NewBool(ctx, ref && ref->prop && ref->data);
    }

    static const JSCFunctionListEntry js_param_ref_proto_funcs[] = {
        JS_CFUNC_DEF("get", 0, js_param_ref_get),
        JS_CFUNC_DEF("set", 1, js_param_ref_set),
        JS_CFUNC_DEF("toString", 0, js_param_ref_tostring),
        JS_CFUNC_DEF("isValid", 0, js_param_ref_is_valid),
    };

    static bool s_param_ref_class_registered = false;
    static JSValue s_param_ref_proto = JS_UNDEFINED;

    void ensure_param_ref_class(JSContext* ctx)
    {
        if (s_param_ref_class_registered) return;
        JS_NewClassID(JS_GetRuntime(ctx), &js_param_ref_class_id);
        JS_NewClass(JS_GetRuntime(ctx), js_param_ref_class_id, &js_param_ref_class);
        s_param_ref_proto = JS_NewObject(ctx);
        JS_SetPropertyFunctionList(ctx, s_param_ref_proto, js_param_ref_proto_funcs,
            sizeof(js_param_ref_proto_funcs) / sizeof(js_param_ref_proto_funcs[0]));
        JS_SetClassProto(ctx, js_param_ref_class_id, JS_DupValue(ctx, s_param_ref_proto));
        s_param_ref_class_registered = true;
    }

    void reset_param_ref_class(JSContext* ctx)
    {
        if (!s_param_ref_class_registered) return;
        if (ctx && !JS_IsUndefined(s_param_ref_proto))
            JS_FreeValue(ctx, s_param_ref_proto);
        s_param_ref_proto = JS_UNDEFINED;
        js_param_ref_class_id = 0;
        s_param_ref_class_registered = false;
    }

    JSValue create_param_ref(JSContext* ctx, Unreal::FProperty* prop, void* data)
    {
        ensure_param_ref_class(ctx);
        JSValue obj = JS_NewObjectClass(ctx, js_param_ref_class_id);
        if (JS_IsException(obj)) return obj;
        auto* ref = static_cast<ParamRefData*>(js_malloc(ctx, sizeof(ParamRefData)));
        ref->prop = prop;
        ref->data = data;
        JS_SetOpaque(obj, ref);
        return obj;
    }

    // ============================================
    // Property <-> JSValue conversion
    // ============================================

    // SEH-isolated write probe: verify target memory is writable
    static int seh_probe_write(void* data)
    {
        if (!data) return 0;
        __try
        {
            volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
            volatile uint8_t old = *p;
            *p = old;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void jsvalue_to_property(JSContext* ctx, Unreal::FProperty* prop, void* data, JSValue val)
    {
        if (!prop || !data) return;
        if (!seh_probe_write(data))
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] jsvalue_to_property: target memory not writable (stale pointer)\n"));
            return;
        }

        try
        {
            if (prop->IsA<Unreal::FStrProperty>())
            {
                const char* cstr = JS_ToCString(ctx, val);
                if (cstr)
                {
                    std::wstring wide = utf8_to_wide(std::string(cstr));
                    JS_FreeCString(ctx, cstr);
                    Unreal::FString temp_str(wide.c_str());
                    prop->CopySingleValue(data, &temp_str);
                }
                return;
            }

            if (prop->IsA<Unreal::FBoolProperty>())
            {
                auto* bool_prop = static_cast<Unreal::FBoolProperty*>(prop);
                bool_prop->SetPropertyValue(data, JS_ToBool(ctx, val) != 0);
                return;
            }

            if (prop->IsA<Unreal::FNumericProperty>())
            {
                auto* num_prop = static_cast<Unreal::FNumericProperty*>(prop);
                if (num_prop->IsFloatingPoint())
                {
                    double d = 0;
                    JS_ToFloat64(ctx, &d, val);
                    num_prop->SetFloatingPointPropertyValue(data, d);
                }
                else if (num_prop->IsInteger())
                {
                    int64_t i = 0;
                    JS_ToInt64(ctx, &i, val);
                    num_prop->SetIntPropertyValue(data, static_cast<uint64_t>(i));
                }
                return;
            }

            if (prop->IsA<Unreal::FNameProperty>())
            {
                const char* cstr = JS_ToCString(ctx, val);
                if (cstr)
                {
                    std::wstring wide = utf8_to_wide(std::string(cstr));
                    JS_FreeCString(ctx, cstr);
                    Unreal::FName temp_name(wide);
                    prop->CopySingleValue(data, &temp_name);
                }
                return;
            }

            if (prop->IsA<Unreal::FTextProperty>())
            {
                const char* cstr = JS_ToCString(ctx, val);
                if (cstr)
                {
                    std::wstring wide = utf8_to_wide(std::string(cstr));
                    JS_FreeCString(ctx, cstr);
                    Unreal::FText* ftext = static_cast<Unreal::FText*>(data);
                    if (ftext)
                    {
                        Unreal::FString fstr(wide.c_str());
                        ftext->SetString(std::move(fstr));
                    }
                }
                return;
            }

            if (prop->IsA<Unreal::FStructProperty>())
            {
                if (!JS_IsObject(val)) return;
                auto* struct_prop = static_cast<Unreal::FStructProperty*>(prop);
                Unreal::UScriptStruct* script_struct = struct_prop->GetStruct();
                if (!script_struct) return;

                for (Unreal::FProperty* field : Unreal::TFieldRange<Unreal::FProperty>(
                         script_struct, Unreal::EFieldIterationFlags::IncludeDeprecated))
                {
                    std::string field_name = wide_to_utf8(field->GetName());
                    JSValue js_field = JS_GetPropertyStr(ctx, val, field_name.c_str());
                    if (!JS_IsUndefined(js_field))
                    {
                        void* field_data = field->ContainerPtrToValuePtr<void>(data);
                        jsvalue_to_property(ctx, field, field_data, js_field);
                    }
                    JS_FreeValue(ctx, js_field);
                }
                return;
            }

            if (prop->IsA<Unreal::FArrayProperty>())
            {
                if (!JS_IsArray(val)) return;
                auto* array_prop = static_cast<Unreal::FArrayProperty*>(prop);
                Unreal::FProperty* inner = const_cast<Unreal::FProperty*>(array_prop->GetInner());
                if (!inner) return;

                JSValue length_value = JS_GetPropertyStr(ctx, val, "length");
                uint32_t length = 0;
                if (JS_ToUint32(ctx, &length, length_value) != 0)
                {
                    JS_FreeValue(ctx, length_value);
                    return;
                }
                JS_FreeValue(ctx, length_value);

                constexpr uint32_t max_js_array_write_elements = 4096;
                if (length > max_js_array_write_elements)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.JavaScript] jsvalue_to_property: refusing to write oversized array ({} elements)\n"),
                        length);
                    return;
                }

                Unreal::FScriptArrayHelper helper(array_prop, data);
                helper.Resize(static_cast<int32_t>(length));
                for (uint32_t i = 0; i < length; i++)
                {
                    JSValue item = JS_GetPropertyUint32(ctx, val, i);
                    void* elem_data = helper.GetRawPtr(static_cast<int32_t>(i));
                    jsvalue_to_property(ctx, inner, elem_data, item);
                    JS_FreeValue(ctx, item);
                }
                return;
            }

            if (prop->IsA<Unreal::FObjectProperty>())
            {
                if (JS_IsNull(val))
                {
                    *static_cast<Unreal::UObject**>(data) = nullptr;
                }
                else
                {
                    void* obj = JSUObject::get_uobject(ctx, val);
                    if (obj)
                        *static_cast<Unreal::UObject**>(data) = static_cast<Unreal::UObject*>(obj);
                }
                return;
            }

            if (prop->IsA<Unreal::FInterfaceProperty>())
            {
                auto* script_interface = static_cast<Unreal::FScriptInterface*>(data);
                if (!script_interface)
                {
                    return;
                }

                script_interface->ObjectPointer = nullptr;
                script_interface->InterfacePointer = nullptr;

                if (!JS_IsNull(val))
                {
                    void* obj = JSUObject::get_uobject(ctx, val);
                    if (obj)
                    {
                        script_interface->ObjectPointer = static_cast<Unreal::UObject*>(obj);
                    }
                }
                return;
            }

            if (prop->IsA<Unreal::FEnumProperty>())
            {
                auto* enum_prop = static_cast<Unreal::FEnumProperty*>(prop);
                auto* underlying = enum_prop->GetUnderlyingProperty();
                if (underlying)
                {
                    int64_t i = 0;
                    JS_ToInt64(ctx, &i, val);
                    underlying->SetIntPropertyValue(data, static_cast<uint64_t>(i));
                }
                return;
            }

            if (prop->IsA<Unreal::FByteProperty>())
            {
                int64_t i = 0;
                JS_ToInt64(ctx, &i, val);
                *static_cast<uint8_t*>(data) = static_cast<uint8_t>(i);
                return;
            }
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] jsvalue_to_property exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] jsvalue_to_property: unknown exception\n"));
        }
    }

    static int seh_read_fstring_safe(void* fstr_raw, const wchar_t** out_data, int32_t* out_len)
    {
        *out_data = nullptr;
        *out_len = 0;
        __try
        {
            auto* fstr = static_cast<Unreal::FString*>(fstr_raw);
            if (!fstr) return 0;
            int32_t num = fstr->GetCharArray().Num();
            if (num <= 1) return 0;
            const wchar_t* d = fstr->GetCharArray().GetData();
            if (!d) return 0;
            volatile wchar_t probe = *d;
            (void)probe;
            *out_data = d;
            *out_len = num - 1;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void read_fname_safe_inner(void* fname_raw, std::wstring& out_value)
    {
        auto* fname = static_cast<Unreal::FName*>(fname_raw);
        if (!fname)
        {
            out_value.clear();
            return;
        }
        out_value = fname->ToString();
    }

    static int seh_read_fname_safe(void* fname_raw, std::wstring& out_value)
    {
        out_value.clear();
        if (!fname_raw) return 0;
        __try
        {
            read_fname_safe_inner(fname_raw, out_value);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out_value.clear();
            return 0;
        }
    }

    static void read_ftext_safe_inner(void* ftext_raw, std::wstring& out_value)
    {
        auto* ftext = static_cast<Unreal::FText*>(ftext_raw);
        if (!ftext || !ftext->Data)
        {
            out_value.clear();
            return;
        }
        out_value = ftext->ToString();
    }

    static int seh_read_ftext_safe(void* ftext_raw, std::wstring& out_value)
    {
        out_value.clear();
        if (!ftext_raw) return 0;
        __try
        {
            read_ftext_safe_inner(ftext_raw, out_value);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out_value.clear();
            return 0;
        }
    }

    static JSValue property_to_jsvalue_inner(JSContext* ctx, Unreal::FProperty* prop, void* data)
    {
        try
        {
            if (prop->IsA<Unreal::FStrProperty>())
            {
                const wchar_t* wstr = nullptr;
                int32_t wstr_len = 0;
                if (seh_read_fstring_safe(data, &wstr, &wstr_len) && wstr && wstr_len > 0)
                {
                    std::string utf8_str = wide_to_utf8(std::wstring(wstr, wstr_len));
                    return JS_NewString(ctx, utf8_str.c_str());
                }
                return JS_NewString(ctx, "");
            }

            if (prop->IsA<Unreal::FAnsiStrProperty>())
            {
                try
                {
                    Unreal::FAnsiString* astr = static_cast<Unreal::FAnsiString*>(data);
                    if (astr && !astr->IsEmpty())
                    {
                        const char* cdata = astr->GetCharArray().GetData();
                        if (cdata)
                            return JS_NewString(ctx, cdata);
                    }
                }
                catch (...) {}
                return JS_NewString(ctx, "");
            }

            if (prop->IsA<Unreal::FNameProperty>())
            {
                std::wstring wide_str;
                if (seh_read_fname_safe(data, wide_str))
                {
                    std::string utf8_str = wide_to_utf8(wide_str);
                    return JS_NewString(ctx, utf8_str.c_str());
                }
                return JS_NewString(ctx, "");
            }

            if (prop->IsA<Unreal::FBoolProperty>())
            {
                Unreal::FBoolProperty* bool_prop = static_cast<Unreal::FBoolProperty*>(prop);
                bool value = bool_prop->GetPropertyValue(data);
                return JS_NewBool(ctx, value);
            }

            if (prop->IsA<Unreal::FNumericProperty>())
            {
                Unreal::FNumericProperty* num_prop = static_cast<Unreal::FNumericProperty*>(prop);
                if (num_prop->IsFloatingPoint())
                {
                    double value = num_prop->GetFloatingPointPropertyValue(data);
                    return JS_NewFloat64(ctx, value);
                }
                else if (num_prop->IsInteger())
                {
                    int64_t value = num_prop->GetSignedIntPropertyValue(data);
                    return JS_NewInt64(ctx, value);
                }
            }

            if (prop->IsA<Unreal::FObjectProperty>())
            {
                try
                {
                    Unreal::UObject** obj_ptr = static_cast<Unreal::UObject**>(data);
                    if (obj_ptr && *obj_ptr)
                    {
                        return JSUObject::create(ctx, *obj_ptr);
                    }
                }
                catch (...) {}
                return JS_NULL;
            }

            if (prop->IsA<Unreal::FInterfaceProperty>())
            {
                try
                {
                    auto* script_interface = static_cast<Unreal::FScriptInterface*>(data);
                    if (script_interface && script_interface->ObjectPointer)
                    {
                        return JSUObject::create(ctx, script_interface->ObjectPointer);
                    }
                }
                catch (...) {}
                return JS_NULL;
            }

            if (prop->IsA<Unreal::FTextProperty>())
            {
                std::wstring wide_str;
                if (seh_read_ftext_safe(data, wide_str))
                {
                    std::string utf8_str = wide_to_utf8(wide_str);
                    return JS_NewString(ctx, utf8_str.c_str());
                }
                return JS_NewString(ctx, "");
            }

            if (prop->IsA<Unreal::FStructProperty>())
            {
                auto* struct_prop = static_cast<Unreal::FStructProperty*>(prop);
                Unreal::UScriptStruct* script_struct = struct_prop->GetStruct();
                if (script_struct)
                {
                    JSValue js_obj = JS_NewObject(ctx);
                    for (Unreal::FProperty* field : Unreal::TFieldRange<Unreal::FProperty>(
                             script_struct, Unreal::EFieldIterationFlags::IncludeDeprecated))
                    {
                        void* field_data = field->ContainerPtrToValuePtr<void>(data);
                        std::string field_name = wide_to_utf8(field->GetName());
                        JSValue field_val = property_to_jsvalue(ctx, field, field_data);
                        JS_SetPropertyStr(ctx, js_obj, field_name.c_str(), field_val);
                    }
                    return js_obj;
                }
                return JS_NULL;
            }

            if (prop->IsA<Unreal::FArrayProperty>())
            {
                auto* array_prop = static_cast<Unreal::FArrayProperty*>(prop);
                Unreal::FScriptArrayHelper helper(array_prop, data);
                int32_t count = helper.Num();
                JSValue js_arr = JS_NewArray(ctx);
                Unreal::FProperty* inner = const_cast<Unreal::FProperty*>(array_prop->GetInner());
                for (int32_t i = 0; i < count; i++)
                {
                    void* elem_data = helper.GetRawPtr(i);
                    JSValue elem_val = inner ? property_to_jsvalue(ctx, inner, elem_data) : JS_NULL;
                    JS_SetPropertyUint32(ctx, js_arr, static_cast<uint32_t>(i), elem_val);
                }
                return js_arr;
            }

            if (prop->IsA<Unreal::FEnumProperty>())
            {
                auto* enum_prop = static_cast<Unreal::FEnumProperty*>(prop);
                Unreal::FNumericProperty* underlying = enum_prop->GetUnderlyingProperty();
                if (underlying && underlying->IsInteger())
                {
                    const int64_t value = underlying->GetSignedIntPropertyValue(data);
                    return JS_NewInt64(ctx, value);
                }
                return JS_NULL;
            }

            if (prop->IsA<Unreal::FByteProperty>())
            {
                const uint8_t byte_val = *static_cast<uint8_t*>(data);
                return JS_NewInt32(ctx, static_cast<int32_t>(byte_val));
            }
            
            // TODO: Add support for other property types
            // Output::send<LogLevel::Warning>(
            //     STR("[UE4SSL.JavaScript] property_to_jsvalue_inner: unsupported property kind for read\n"));
            return JS_NULL;
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] property_to_jsvalue_inner exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NULL;
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] property_to_jsvalue_inner: unknown exception\n"));
            return JS_NULL;
        }
    }

    // SEH probe: verify the property memory is readable before doing the full conversion.
    // Separated to satisfy MSVC C2712 (no C++ objects in __try scope).
    static int seh_probe_property(Unreal::FProperty* prop, void* data)
    {
        __try
        {
            volatile uint8_t probe = *static_cast<volatile uint8_t*>(data);
            (void)probe;
            volatile auto size = prop->GetSize();
            (void)size;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    JSValue property_to_jsvalue(JSContext* ctx, Unreal::FProperty* prop, void* data)
    {
        if (!prop || !data)
            return JS_NULL;

        if (!seh_probe_property(prop, data))
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] property_to_jsvalue: SEH caught access violation on stale property memory\n"));
            return JS_NULL;
        }

        return property_to_jsvalue_inner(ctx, prop, data);
    }

    // ============================================
    // Module Loader
    // ============================================

    char* js_module_normalize(JSContext* ctx, const char* base_name, const char* name, void* opaque)
    {
        (void)opaque;

        std::filesystem::path base;
        std::filesystem::path result;
#if defined(_WIN32)
        base = std::filesystem::path(utf8_to_wide(base_name));
#else
        base = std::filesystem::path(base_name);
#endif

        if (name[0] == '.' && (name[1] == '/' || (name[1] == '.' && name[2] == '/')))
        {
            result = base.parent_path() / name;
        }
        else
        {
            result = base.parent_path() / name;
        }

        if (result.extension() != ".js")
        {
            result += ".js";
        }

        result = result.lexically_normal();

#if defined(_WIN32)
        std::string path_str = wide_to_utf8(result.wstring());
#else
        std::string path_str = result.string();
#endif
        char* ret = static_cast<char*>(js_malloc(ctx, path_str.size() + 1));
        if (ret)
        {
            memcpy(ret, path_str.c_str(), path_str.size() + 1);
        }
        return ret;
    }

    JSModuleDef* js_module_loader(JSContext* ctx, const char* module_name, void* opaque)
    {
        auto* mod = static_cast<JSMod*>(opaque);
        if (!mod || !module_name)
        {
            JS_ThrowReferenceError(ctx, "Module loader: invalid arguments");
            return nullptr;
        }

        try
        {
            std::string name_str(module_name);
            if (mod->m_loaded_modules.count(name_str) > 0)
            {
                // QuickJS handles caching itself
            }

#if defined(_WIN32)
            std::wstring wpath = utf8_to_wide(module_name);
            std::filesystem::path path_obj(wpath);
            std::ifstream file(path_obj, std::ios::binary);
#else
            std::ifstream file(module_name, std::ios::binary);
#endif
            if (!file.is_open())
            {
                JS_ThrowReferenceError(ctx, "Could not open module '%s'", module_name);
                return nullptr;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            file.close();

            JSValue func_val = JS_UNDEFINED;
            if (!safe_js_eval(ctx, content.c_str(), content.size(),
                              module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY, &func_val))
            {
                JS_ThrowReferenceError(ctx, "Module loader SEH exception for '%s'", module_name);
                return nullptr;
            }

            if (JS_IsException(func_val))
            {
                return nullptr;
            }

            mod->m_loaded_modules[name_str] = true;

            JSModuleDef* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func_val));
            JS_FreeValue(ctx, func_val);

            return m;
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Module loader exception for '{}': {}\n"),
                std::wstring(module_name, module_name + strlen(module_name)),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            JS_ThrowReferenceError(ctx, "Module loader exception: %s", e.what());
            return nullptr;
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Module loader unknown exception for '{}'\n"),
                std::wstring(module_name, module_name + strlen(module_name)));
            JS_ThrowReferenceError(ctx, "Module loader unknown exception for '%s'", module_name);
            return nullptr;
        }
    }

} // namespace RC::JSScript
