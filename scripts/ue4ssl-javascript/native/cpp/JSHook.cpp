#include "JSInternal.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <sstream>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UE4SSProgram.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/Property/FTextProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Input/Handler.hpp>

namespace RC::JSScript
{
    namespace
    {
        static auto debug_json_quote(const std::string& value) -> std::string
        {
            std::string out;
            out.reserve(value.size() + 8);
            out.push_back('"');
            for (char ch : value)
            {
                switch (ch)
                {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out.push_back(ch); break;
                }
            }
            out.push_back('"');
            return out;
        }

    }

    // ============================================
    // SEH helpers for hook system
    // ============================================

    static Unreal::UFunction* safe_get_current_native_function(Unreal::UnrealScriptFunctionCallableContext& context)
    {
        __try
        {
            return context.TheStack.CurrentNativeFunction();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return NULL;
        }
    }

    struct ExtractedParam { Unreal::FProperty* prop; void* data; };

    static int safe_extract_hook_params_inner(
        Unreal::UFunction* func,
        void* locals,
        void* out_parms_head,
        ExtractedParam* buf,
        int buf_capacity,
        int* out_count,
        bool* out_has_return_value)
    {
        *out_count = 0;
        *out_has_return_value = false;
        __try
        {
            uint16_t return_value_offset = func->GetReturnValueOffset();
            *out_has_return_value = return_value_offset != 0xFFFF;

            for (Unreal::FProperty* func_prop : Unreal::TFieldRange<Unreal::FProperty>(func, Unreal::EFieldIterationFlags::IncludeDeprecated))
            {
                if (!func_prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm))
                    continue;
                if (func_prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
                    continue;

                void* param_data = NULL;
                if (func_prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_OutParm) && !func_prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ConstParm))
                {
                    typedef struct FOutParmRec_ { Unreal::FProperty* Property; uint8_t* PropAddr; struct FOutParmRec_* NextOutParm; } FOutParmRec_;
                    FOutParmRec_* out_param = (FOutParmRec_*)out_parms_head;
                    param_data = out_parms_head;
                    while (out_param)
                    {
                        if (out_param->Property == func_prop)
                        {
                            param_data = out_param->PropAddr;
                            break;
                        }
                        out_param = out_param->NextOutParm;
                    }
                }
                else
                {
                    param_data = func_prop->ContainerPtrToValuePtr<void>(locals);
                }

                if (*out_count < buf_capacity)
                {
                    buf[*out_count].prop = func_prop;
                    buf[*out_count].data = param_data;
                    (*out_count)++;
                }
            }
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] safe_extract_hook_params: access violation during parameter extraction\n"));
            return 0;
        }
    }

    static bool safe_extract_hook_params(
        Unreal::UFunction* func,
        Unreal::UnrealScriptFunctionCallableContext& context,
        std::vector<std::pair<Unreal::FProperty*, void*>>& out_params,
        bool& out_has_return_value)
    {
        out_has_return_value = false;
        if (!safe_has_iterable_properties(func)) return true;
        if (!context.TheStack.Locals() && !context.TheStack.OutParms()) return true;

        constexpr int MAX_PARAMS = 32;
        ExtractedParam param_buf[MAX_PARAMS];
        int count = 0;
        bool has_ret = false;

        if (!safe_extract_hook_params_inner(func, context.TheStack.Locals(), context.TheStack.OutParms(), param_buf, MAX_PARAMS, &count, &has_ret))
        {
            return false;
        }

        out_has_return_value = has_ret;
        for (int i = 0; i < count; i++)
        {
            out_params.push_back({param_buf[i].prop, param_buf[i].data});
        }
        return true;
    }

    static bool safe_probe_hook_data(void* custom_data)
    {
        if (!custom_data) return false;
        __try
        {
            JSMod::JSUFunctionHookData* hd = (JSMod::JSUFunctionHookData*)custom_data;
            volatile void* ctx_probe = (volatile void*)hd->ctx;
            volatile void* owner_probe = (volatile void*)hd->owner;
            return ctx_probe != NULL && owner_probe != NULL;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool safe_engine_unregister_hook(Unreal::UFunction* function, Unreal::CallbackId pre_id, Unreal::CallbackId post_id)
    {
        __try
        {
            Unreal::UObjectGlobals::UnregisterHook(function, {pre_id, post_id});
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] UnregisterHook: UFunction pointer is stale, skipping engine unregister\n"));
            return false;
        }
    }

    static std::wstring safe_get_function_path(Unreal::UFunction* function)
    {
        if (!function)
        {
            return {};
        }

        try
        {
            return function->GetPathName();
        }
        catch (...)
        {
            return {};
        }
    }

    static Unreal::UFunction* resolve_function_by_path(const std::wstring& function_path)
    {
        if (function_path.empty())
        {
            return nullptr;
        }

        try
        {
            return Unreal::UObjectGlobals::StaticFindObject<Unreal::UFunction*>(nullptr, nullptr, function_path);
        }
        catch (...)
        {
            return nullptr;
        }
    }

    static void free_bind_hook_callbacks(JSMod::JSBindHookData& bind_hook)
    {
        if (!bind_hook.ctx)
        {
            bind_hook.pre_callback = JS_UNDEFINED;
            bind_hook.post_callback = JS_UNDEFINED;
            return;
        }

        if (!JS_IsUndefined(bind_hook.pre_callback))
        {
            JS_FreeValue(bind_hook.ctx, bind_hook.pre_callback);
            bind_hook.pre_callback = JS_UNDEFINED;
        }
        if (!JS_IsUndefined(bind_hook.post_callback))
        {
            JS_FreeValue(bind_hook.ctx, bind_hook.post_callback);
            bind_hook.post_callback = JS_UNDEFINED;
        }
    }

    static void enqueue_bind_hook_activation(JSMod* mod, const std::wstring& function_path)
    {
        if (!mod || function_path.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mod->m_pending_bind_hook_activations_mutex);
        mod->m_pending_bind_hook_activations.push_back({function_path});
    }

    static bool ensure_bind_watch_registered(JSMod* mod)
    {
        if (!mod)
        {
            return false;
        }

        if (mod->m_bind_watch_callback_id != Unreal::Hook::ERROR_ID)
        {
            return true;
        }

        Unreal::Hook::FCallbackOptions opts{};
        opts.OwnerModName = STR("UE4SSL.JavaScript");
        opts.HookName = STR("JavaScriptBindHookWatch");

        mod->m_bind_watch_callback_id = Unreal::Hook::RegisterUFunctionBindPostCallback(
            [mod](Unreal::Hook::TCallbackIterationData<void>&, Unreal::UFunction* function) {
                if (!mod || !function)
                {
                    return;
                }

                std::wstring function_path = safe_get_function_path(function);
                if (function_path.empty())
                {
                    return;
                }

                bool should_enqueue = false;
                {
                    std::lock_guard<std::mutex> lock(mod->m_bind_hooks_mutex);
                    for (const auto& bind_hook : mod->m_bind_hooks)
                    {
                        if (bind_hook && bind_hook->function_path == function_path)
                        {
                            should_enqueue = true;
                            break;
                        }
                    }
                }

                if (should_enqueue)
                {
                    enqueue_bind_hook_activation(mod, function_path);
                }
            },
            opts
        );

        if (mod->m_bind_watch_callback_id == Unreal::Hook::ERROR_ID)
        {
            Output::send<LogLevel::Warning>(
                STR("[UE4SSL.JavaScript] RegisterUFunctionBindPostCallback failed. "
                    "RegisterBindHook can only activate already-loaded UFunctions. "
                    "Ensure HookUFunctionBind = 1 in UE4SS-settings.ini\n"));
            return false;
        }

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Registered shared UFunctionBind watcher\n"));
        return true;
    }

    static void deactivate_bind_hook_instance(JSMod* mod, JSMod::JSBindHookData& bind_hook)
    {
        if (!mod)
        {
            return;
        }

        if (bind_hook.active_pre_id != 0 || bind_hook.active_post_id != 0)
        {
            (void)mod->unregister_ufunction_hook(bind_hook.active_pre_id, bind_hook.active_post_id);
        }

        bind_hook.active_pre_id = 0;
        bind_hook.active_post_id = 0;
        bind_hook.current_function = nullptr;
    }

    static bool activate_bind_hook_instance(JSMod* mod, JSMod::JSBindHookData& bind_hook, Unreal::UFunction* function, bool log_failure)
    {
        if (!mod || !function)
        {
            return false;
        }

        deactivate_bind_hook_instance(mod, bind_hook);

        try
        {
            auto [pre_id, post_id] = mod->register_ufunction_hook(
                bind_hook.ctx,
                function,
                bind_hook.pre_callback,
                bind_hook.post_callback);

            if (pre_id == 0 && post_id == 0)
            {
                if (log_failure)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.JavaScript] RegisterBindHook activation returned invalid ids for {}\n"),
                        bind_hook.function_path);
                }
                return false;
            }

            bind_hook.current_function = function;
            bind_hook.active_pre_id = pre_id;
            bind_hook.active_post_id = post_id;

            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] Bind hook activated for {} (pre_id={}, post_id={})\n"),
                bind_hook.function_path, pre_id, post_id);
            return true;
        }
        catch (const std::exception& e)
        {
            if (log_failure)
            {
                Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.JavaScript] RegisterBindHook activation failed for {}: {}\n"),
                    bind_hook.function_path,
                    std::wstring(e.what(), e.what() + strlen(e.what())));
            }
        }
        catch (...)
        {
            if (log_failure)
            {
                Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.JavaScript] RegisterBindHook activation hit an unknown exception for {}\n"),
                    bind_hook.function_path);
            }
        }

        return false;
    }

    // SEH probe for raw UE parameter data pointer
    static int seh_probe_param_data(void* data)
    {
        if (!data) return 0;
        __try
        {
            volatile uint8_t probe = *static_cast<volatile uint8_t*>(data);
            (void)probe;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    // SEH-protected read of FString Data pointer + length (avoids wcslen on invalid pointer)
    static int seh_read_fstring_data(void* fstr_ptr, const wchar_t** out_data, int32_t* out_len)
    {
        *out_data = nullptr;
        *out_len = 0;
        __try
        {
            auto* fstr = static_cast<Unreal::FString*>(fstr_ptr);
            int32_t num = fstr->GetCharArray().Num();
            if (num <= 1) return 0;
            const wchar_t* data = fstr->GetCharArray().GetData();
            if (!data) return 0;
            volatile wchar_t probe = *data;
            (void)probe;
            *out_data = data;
            *out_len = num - 1;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    // SEH-protected read of FEnumProperty underlying integer value
    static int seh_read_enum_value(Unreal::FProperty* prop, void* data, int64_t* out_val)
    {
        *out_val = 0;
        __try
        {
            auto* enum_prop = static_cast<Unreal::FEnumProperty*>(prop);
            auto* underlying = enum_prop->GetUnderlyingProperty();
            if (underlying)
            {
                *out_val = underlying->GetSignedIntPropertyValue(data);
            }
            else
            {
                *out_val = static_cast<int64_t>(*static_cast<uint8_t*>(data));
            }
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void read_ftext_string_inner(void* data, std::wstring& out_val)
    {
        auto* ftext = static_cast<Unreal::FText*>(data);
        if (!ftext || !ftext->Data)
        {
            out_val.clear();
            return;
        }
        out_val = ftext->ToString();
    }

    static int seh_read_ftext_string(void* data, std::wstring& out_val)
    {
        out_val.clear();
        if (!data) return 0;
        __try
        {
            read_ftext_string_inner(data, out_val);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out_val.clear();
            return 0;
        }
    }

    static void read_fname_string_inner(void* data, std::wstring& out_val)
    {
        auto* fname = static_cast<Unreal::FName*>(data);
        if (!fname)
        {
            out_val.clear();
            return;
        }
        out_val = fname->ToString();
    }

    static int seh_read_fname_string(void* data, std::wstring& out_val)
    {
        out_val.clear();
        if (!data) return 0;
        __try
        {
            read_fname_string_inner(data, out_val);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out_val.clear();
            return 0;
        }
    }

    static bool is_supported_deferred_string_array_inner(Unreal::FProperty* prop)
    {
        return prop
            && (prop->IsA<Unreal::FStrProperty>()
                || prop->IsA<Unreal::FTextProperty>()
                || prop->IsA<Unreal::FNameProperty>());
    }

    static void snapshot_string_array_inner(
        Unreal::FArrayProperty* array_prop,
        void* data,
        std::vector<std::wstring>& out_vals)
    {
        constexpr int32_t max_deferred_string_array_elements = 32;

        out_vals.clear();
        Unreal::FProperty* inner = const_cast<Unreal::FProperty*>(array_prop->GetInner());
        if (!is_supported_deferred_string_array_inner(inner))
        {
            return;
        }

        Unreal::FScriptArrayHelper helper(array_prop, data);
        int32_t count = helper.Num();
        if (count <= 0)
        {
            return;
        }
        if (count > max_deferred_string_array_elements)
        {
            count = max_deferred_string_array_elements;
        }

        out_vals.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; i++)
        {
            std::wstring value;
            void* elem_data = helper.GetRawPtr(i);
            if (inner->IsA<Unreal::FStrProperty>())
            {
                const wchar_t* str_data = nullptr;
                int32_t str_len = 0;
                if (seh_read_fstring_data(elem_data, &str_data, &str_len) && str_data && str_len > 0)
                {
                    value.assign(str_data, str_len);
                }
            }
            else if (inner->IsA<Unreal::FTextProperty>())
            {
                (void)seh_read_ftext_string(elem_data, value);
            }
            else if (inner->IsA<Unreal::FNameProperty>())
            {
                (void)seh_read_fname_string(elem_data, value);
            }
            out_vals.push_back(std::move(value));
        }
    }

    static int seh_snapshot_string_array_param(
        Unreal::FArrayProperty* array_prop,
        void* data,
        std::vector<std::wstring>& out_vals)
    {
        out_vals.clear();
        if (!array_prop || !data) return 0;

        Unreal::FProperty* inner = const_cast<Unreal::FProperty*>(array_prop->GetInner());
        if (!is_supported_deferred_string_array_inner(inner))
        {
            return 0;
        }

        __try
        {
            snapshot_string_array_inner(array_prop, data, out_vals);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out_vals.clear();
            return 0;
        }
    }

    static bool snapshot_property_json_inner(
        Unreal::FProperty* prop,
        void* data,
        std::string& out_json,
        int depth)
    {
        constexpr int max_deferred_json_depth = 6;
        constexpr int32_t max_deferred_json_array_elements = 32;
        constexpr int32_t max_deferred_json_struct_fields = 64;

        if (!prop || !data || depth > max_deferred_json_depth)
        {
            out_json = "null";
            return false;
        }

        if (prop->IsA<Unreal::FStrProperty>())
        {
            const wchar_t* str_data = nullptr;
            int32_t str_len = 0;
            std::wstring value;
            if (seh_read_fstring_data(data, &str_data, &str_len) && str_data && str_len > 0)
            {
                value.assign(str_data, str_len);
            }
            out_json = debug_json_quote(wide_to_utf8(value));
            return true;
        }

        if (prop->IsA<Unreal::FTextProperty>())
        {
            std::wstring value;
            (void)seh_read_ftext_string(data, value);
            out_json = debug_json_quote(wide_to_utf8(value));
            return true;
        }

        if (prop->IsA<Unreal::FNameProperty>())
        {
            std::wstring value;
            (void)seh_read_fname_string(data, value);
            out_json = debug_json_quote(wide_to_utf8(value));
            return true;
        }

        if (prop->IsA<Unreal::FEnumProperty>())
        {
            int64_t value = 0;
            (void)seh_read_enum_value(prop, data, &value);
            out_json = std::to_string(value);
            return true;
        }

        if (prop->IsA<Unreal::FBoolProperty>())
        {
            out_json = static_cast<Unreal::FBoolProperty*>(prop)->GetPropertyValue(data) ? "true" : "false";
            return true;
        }

        if (prop->IsA<Unreal::FNumericProperty>())
        {
            auto* num_prop = static_cast<Unreal::FNumericProperty*>(prop);
            if (num_prop->IsFloatingPoint())
            {
                const double value = num_prop->GetFloatingPointPropertyValue(data);
                if (!std::isfinite(value))
                {
                    out_json = "null";
                    return false;
                }

                std::ostringstream stream;
                stream << value;
                out_json = stream.str();
                return true;
            }

            if (num_prop->IsInteger())
            {
                out_json = std::to_string(num_prop->GetSignedIntPropertyValue(data));
                return true;
            }
        }

        if (prop->IsA<Unreal::FObjectProperty>())
        {
            Unreal::UObject** obj_ptr = static_cast<Unreal::UObject**>(data);
            out_json = (obj_ptr && *obj_ptr)
                ? debug_json_quote(wide_to_utf8((*obj_ptr)->GetPathName()))
                : "null";
            return true;
        }

        if (prop->IsA<Unreal::FArrayProperty>())
        {
            auto* array_prop = static_cast<Unreal::FArrayProperty*>(prop);
            Unreal::FProperty* inner = const_cast<Unreal::FProperty*>(array_prop->GetInner());
            Unreal::FScriptArrayHelper helper(array_prop, data);
            const int32_t count = std::min<int32_t>(helper.Num(), max_deferred_json_array_elements);

            std::ostringstream stream;
            stream << "[";
            for (int32_t i = 0; i < count; ++i)
            {
                if (i > 0)
                {
                    stream << ",";
                }

                void* elem_data = helper.GetRawPtr(i);
                std::string elem_json;
                if (!snapshot_property_json_inner(inner, elem_data, elem_json, depth + 1))
                {
                    elem_json = "null";
                }
                stream << elem_json;
            }
            stream << "]";
            out_json = stream.str();
            return true;
        }

        if (prop->IsA<Unreal::FStructProperty>())
        {
            auto* struct_prop = static_cast<Unreal::FStructProperty*>(prop);
            Unreal::UScriptStruct* script_struct = struct_prop->GetStruct();
            if (!script_struct)
            {
                out_json = "null";
                return false;
            }

            std::ostringstream stream;
            stream << "{";
            bool first_field = true;
            int32_t field_count = 0;
            for (Unreal::FProperty* field : Unreal::TFieldRange<Unreal::FProperty>(
                     script_struct, Unreal::EFieldIterationFlags::IncludeDeprecated))
            {
                if (field_count >= max_deferred_json_struct_fields)
                {
                    break;
                }

                void* field_data = field->ContainerPtrToValuePtr<void>(data);
                std::string field_json;
                if (!snapshot_property_json_inner(field, field_data, field_json, depth + 1))
                {
                    field_json = "null";
                }

                if (!first_field)
                {
                    stream << ",";
                }
                first_field = false;
                stream << debug_json_quote(wide_to_utf8(field->GetName())) << ":" << field_json;
                ++field_count;
            }
            stream << "}";
            out_json = stream.str();
            return true;
        }

        out_json = "null";
        return false;
    }

    static int seh_snapshot_property_json(
        Unreal::FProperty* prop,
        void* data,
        std::string& out_json)
    {
        out_json.clear();
        if (!prop || !data)
        {
            return 0;
        }

        __try
        {
            return snapshot_property_json_inner(prop, data, out_json, 0) ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out_json = "null";
            return 0;
        }
    }

    // Helper: snapshot parameter values from game thread into C++ types for deferred JS execution
    static void snapshot_hook_params(
        const std::vector<std::pair<Unreal::FProperty*, void*>>& params,
        Unreal::UObject* context_object,
        JSMod::JSUFunctionHookData* hook_data,
        bool is_pre)
    {
        JSMod::PendingHookCallback pending;
        pending.hook_data = hook_data;
        pending.is_pre = is_pre;
        pending.context_object = context_object;
        for (auto& [prop, data] : params)
        {
            JSMod::PendingHookCallbackParam p;
            if (!data || !seh_probe_param_data(data))
            {
                pending.params.push_back(p);
                continue;
            }

            try
            {
                if (prop->IsA<Unreal::FStrProperty>())
                {
                    p.type = JSMod::PendingHookCallbackParam::Type::String;
                    const wchar_t* str_data = nullptr;
                    int32_t str_len = 0;
                    if (seh_read_fstring_data(data, &str_data, &str_len) && str_data && str_len > 0)
                    {
                        p.str_val.assign(str_data, str_len);
                    }
                }
                else if (prop->IsA<Unreal::FTextProperty>())
                {
                    p.type = JSMod::PendingHookCallbackParam::Type::String;
                    (void)seh_read_ftext_string(data, p.str_val);
                }
                else if (prop->IsA<Unreal::FNameProperty>())
                {
                    p.type = JSMod::PendingHookCallbackParam::Type::String;
                    (void)seh_read_fname_string(data, p.str_val);
                }
                else if (prop->IsA<Unreal::FArrayProperty>())
                {
                    auto* array_prop = static_cast<Unreal::FArrayProperty*>(prop);
                    if (seh_snapshot_string_array_param(array_prop, data, p.str_array_val))
                    {
                        p.type = JSMod::PendingHookCallbackParam::Type::StringArray;
                    }
                }
                else if (prop->IsA<Unreal::FEnumProperty>())
                {
                    p.type = JSMod::PendingHookCallbackParam::Type::Int;
                    int64_t val = 0;
                    if (seh_read_enum_value(prop, data, &val))
                        p.int_val = val;
                }
                else if (prop->IsA<Unreal::FBoolProperty>())
                {
                    p.type = JSMod::PendingHookCallbackParam::Type::Bool;
                    p.bool_val = static_cast<Unreal::FBoolProperty*>(prop)->GetPropertyValue(data);
                }
                else if (prop->IsA<Unreal::FNumericProperty>())
                {
                    auto* num_prop = static_cast<Unreal::FNumericProperty*>(prop);
                    if (num_prop->IsFloatingPoint()) { p.type = JSMod::PendingHookCallbackParam::Type::Float; p.float_val = num_prop->GetFloatingPointPropertyValue(data); }
                    else if (num_prop->IsInteger()) { p.type = JSMod::PendingHookCallbackParam::Type::Int; p.int_val = num_prop->GetSignedIntPropertyValue(data); }
                }
                else if (prop->IsA<Unreal::FObjectProperty>())
                {
                    p.type = JSMod::PendingHookCallbackParam::Type::Object;
                    Unreal::UObject** obj_ptr = static_cast<Unreal::UObject**>(data);
                    p.obj_val = (obj_ptr && *obj_ptr) ? *obj_ptr : nullptr;
                }
                else if (prop->IsA<Unreal::FStructProperty>())
                {
                    std::string json_snapshot;
                    if (seh_snapshot_property_json(prop, data, json_snapshot))
                    {
                        p.type = JSMod::PendingHookCallbackParam::Type::Json;
                        p.str_val = utf8_to_wide(json_snapshot);
                    }
                }
            }
            catch (...)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] snapshot_hook_params: exception reading param, skipping\n"));
                p = {};
            }
            pending.params.push_back(std::move(p));
        }
        {
            std::lock_guard<std::mutex> lock(hook_data->owner->m_pending_hook_callbacks_mutex);
            hook_data->owner->m_pending_hook_callbacks.push_back(std::move(pending));
        }
    }

    // Helper: direct JS execution of hook callback (on event loop thread or with OutParams)
    static void execute_hook_callback_direct(
        JSMod::JSUFunctionHookData* hook_data,
        Unreal::UnrealScriptFunctionCallableContext& context,
        const std::vector<std::pair<Unreal::FProperty*, void*>>& params,
        bool is_pre,
        bool on_event_loop_thread)
    {
        std::unique_lock<std::recursive_mutex> js_lock(hook_data->owner->m_js_mutex);
        (void)on_event_loop_thread;

        hook_data->is_executing = true;

        JSContext* ctx = hook_data->ctx;
        JSValue callback = is_pre ? hook_data->pre_callback : hook_data->post_callback;

        JSValue args[3];
        args[0] = JSUObject::create(ctx, context.Context);
        JSValue js_params = JS_NewArray(ctx);
        for (size_t i = 0; i < params.size(); i++)
        {
            bool is_out = params[i].first->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_OutParm)
                       && !params[i].first->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ConstParm);
            JSValue js_param = is_out
                ? create_param_ref(ctx, params[i].first, params[i].second)
                : property_to_jsvalue(ctx, params[i].first, params[i].second);
            JS_SetPropertyUint32(ctx, js_params, static_cast<uint32_t>(i), js_param);
        }
        args[1] = js_params;
        args[2] = JS_NewBigInt64(ctx, reinterpret_cast<int64_t>(context.RESULT_DECL));

        JSValue result = JS_UNDEFINED;
        if (safe_js_call(ctx, callback, JS_UNDEFINED, 3, args, &result))
        {
            if (JS_IsException(result))
            {
                std::wstring function_path = safe_get_function_path(hook_data->function);
                hook_data->owner->log_exception(
                    ctx,
                    is_pre ? L"PreHookCallback" : L"PostHookCallback",
                    function_path.empty() ? L"(unknown function)" : function_path);
                hook_data->owner->report_subsystem_failure(JSMod::GuardedSubsystem::Hook,
                    is_pre ? L"PreHookCallback" : L"PostHookCallback",
                    L"JS exception");
            }
            else
            {
                hook_data->owner->report_subsystem_success(JSMod::GuardedSubsystem::Hook);
            }
        }
        else
        {
            hook_data->owner->report_subsystem_failure(JSMod::GuardedSubsystem::Hook,
                is_pre ? L"PreHookCallback" : L"PostHookCallback",
                L"SEH exception");
            result = JS_UNDEFINED;
        }

        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
        JS_FreeValue(ctx, args[2]);

        hook_data->is_executing = false;
    }

    static bool fill_native_method_arg(Unreal::FProperty* prop, void* prop_addr, const JSMod::NativeMethodArg& arg)
    {
        if (!prop || !prop_addr)
        {
            return false;
        }

        try
        {
            if (prop->IsA<Unreal::FBoolProperty>())
            {
                bool value = false;
                if (arg.type == JSMod::NativeMethodArg::Type::Bool) value = arg.bool_value;
                else if (arg.type == JSMod::NativeMethodArg::Type::Int) value = arg.int_value != 0;
                else if (arg.type == JSMod::NativeMethodArg::Type::Float) value = arg.float_value != 0.0;
                static_cast<Unreal::FBoolProperty*>(prop)->SetPropertyValue(prop_addr, value);
                return true;
            }

            if (prop->IsA<Unreal::FNumericProperty>())
            {
                auto* num_prop = static_cast<Unreal::FNumericProperty*>(prop);
                if (num_prop->IsFloatingPoint())
                {
                    double value = 0.0;
                    if (arg.type == JSMod::NativeMethodArg::Type::Float) value = arg.float_value;
                    else if (arg.type == JSMod::NativeMethodArg::Type::Int) value = static_cast<double>(arg.int_value);
                    else if (arg.type == JSMod::NativeMethodArg::Type::Bool) value = arg.bool_value ? 1.0 : 0.0;
                    num_prop->SetFloatingPointPropertyValue(prop_addr, value);
                    return true;
                }
                if (num_prop->IsInteger())
                {
                    int64_t value = 0;
                    if (arg.type == JSMod::NativeMethodArg::Type::Int) value = arg.int_value;
                    else if (arg.type == JSMod::NativeMethodArg::Type::Float) value = static_cast<int64_t>(arg.float_value);
                    else if (arg.type == JSMod::NativeMethodArg::Type::Bool) value = arg.bool_value ? 1 : 0;
                    num_prop->SetIntPropertyValue(prop_addr, value);
                    return true;
                }
            }

            if (prop->IsA<Unreal::FNameProperty>() && arg.type == JSMod::NativeMethodArg::Type::String)
            {
                new (prop_addr) Unreal::FName(arg.string_value.c_str(), Unreal::FNAME_Add);
                return true;
            }

        }
        catch (...)
        {
            return false;
        }

        return false;
    }

    static bool full_name_contains(Unreal::UObject* object, const std::wstring& needle)
    {
        if (!object || needle.empty())
        {
            return true;
        }

        try
        {
            return object->GetFullName().find(needle) != std::wstring::npos;
        }
        catch (...)
        {
            return false;
        }
    }

    static void log_native_method_hook(
        JSMod::NativeObjectMethodHookData* hook_data,
        const wchar_t* phase,
        const wchar_t* status,
        Unreal::UObject* target = nullptr,
        const std::wstring& detail = {})
    {
        if (!hook_data || hook_data->execution_log_count >= 16)
        {
            return;
        }

        hook_data->execution_log_count++;

        std::wstring target_name = STR("<null>");
        if (target)
        {
            try
            {
                target_name = target->GetFullName();
            }
            catch (...)
            {
                target_name = STR("<name-error>");
            }
        }

        std::wstring detail_text{};
        if (!detail.empty())
        {
            detail_text = STR(" detail=");
            detail_text += detail;
        }

        Output::send<LogLevel::Normal>(
            STR("[UE4SSL.JavaScript] NativeObjectMethodHook {} {} method={} target={}{}\n"),
            phase ? phase : L"?",
            status ? status : L"?",
            hook_data->method_name,
            target_name,
            detail_text);
    }

    static std::wstring property_flags_summary(Unreal::FProperty* prop)
    {
        if (!prop)
        {
            return {};
        }

        std::wstring flags{};
        if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm)) flags += STR(" Parm");
        if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_OutParm)) flags += STR(" Out");
        if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm)) flags += STR(" Return");
        if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ConstParm)) flags += STR(" Const");
        if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReferenceParm)) flags += STR(" Ref");
        return flags.empty() ? STR(" none") : flags;
    }

    static std::wstring describe_param_value(Unreal::FProperty* prop, void* data)
    {
        if (!prop || !data)
        {
            return STR("<null-data>");
        }

        try
        {
            if (prop->IsA<Unreal::FObjectProperty>())
            {
                auto** object_ptr = static_cast<Unreal::UObject**>(data);
                Unreal::UObject* object = object_ptr ? *object_ptr : nullptr;
                if (!object)
                {
                    return STR("Object=null");
                }
                return STR("Object=") + object->GetFullName();
            }

            if (prop->IsA<Unreal::FArrayProperty>())
            {
                auto* array_prop = static_cast<Unreal::FArrayProperty*>(prop);
                Unreal::FScriptArrayHelper helper(array_prop, data);
                std::wstring inner_type = STR("<null-inner>");
                Unreal::FProperty* inner = const_cast<Unreal::FProperty*>(array_prop->GetInner());
                if (inner)
                {
                    inner_type = inner->GetClass().GetName();
                }
                return STR("Array.Num=") + std::to_wstring(helper.Num()) + STR(" Inner=") + inner_type;
            }

            if (prop->IsA<Unreal::FBoolProperty>())
            {
                bool value = static_cast<Unreal::FBoolProperty*>(prop)->GetPropertyValue(data);
                return value ? STR("Bool=true") : STR("Bool=false");
            }

            if (prop->IsA<Unreal::FNumericProperty>())
            {
                auto* num_prop = static_cast<Unreal::FNumericProperty*>(prop);
                if (num_prop->IsFloatingPoint())
                {
                    return STR("Float=") + std::to_wstring(num_prop->GetFloatingPointPropertyValue(data));
                }
                if (num_prop->IsInteger())
                {
                    return STR("Int=") + std::to_wstring(num_prop->GetSignedIntPropertyValue(data));
                }
            }

            if (prop->IsA<Unreal::FNameProperty>())
            {
                auto* name = static_cast<Unreal::FName*>(data);
                return name ? STR("Name=") + name->ToString() : STR("Name=<null>");
            }
        }
        catch (...)
        {
            return STR("<value-error>");
        }

        return STR("<unsupported-value>");
    }

    static void diagnose_native_hook_params(
        JSMod::NativeObjectMethodHookData* hook_data,
        Unreal::UnrealScriptFunctionCallableContext& context,
        Unreal::UFunction* current_func,
        const std::vector<std::pair<Unreal::FProperty*, void*>>& params,
        bool has_return_value,
        const wchar_t* phase)
    {
        if (!hook_data || !hook_data->diagnose_params || hook_data->execution_log_count >= 16)
        {
            return;
        }

        hook_data->execution_log_count++;

        std::wstring context_name = STR("<null>");
        if (context.Context)
        {
            try
            {
                context_name = context.Context->GetFullName();
            }
            catch (...)
            {
                context_name = STR("<context-name-error>");
            }
        }

        Output::send<LogLevel::Normal>(
            STR("[UE4SSL.JavaScript] NativeHookDiagnostics {} label={} function={} context={} params={} hasReturn={} locals={} outParms={} result={}\n"),
            phase ? phase : L"?",
            hook_data->diagnostic_label,
            current_func ? current_func->GetFullName() : STR("<null-function>"),
            context_name,
            params.size(),
            has_return_value ? STR("true") : STR("false"),
            context.TheStack.Locals() ? STR("yes") : STR("no"),
            context.TheStack.OutParms() ? STR("yes") : STR("no"),
            context.RESULT_DECL ? STR("yes") : STR("no"));

        for (size_t i = 0; i < params.size(); i++)
        {
            Unreal::FProperty* prop = params[i].first;
            void* data = params[i].second;
            std::wstring prop_name = STR("<null>");
            std::wstring prop_type = STR("<null>");
            if (prop)
            {
                try
                {
                    prop_name = prop->GetName();
                    prop_type = prop->GetClass().GetName();
                }
                catch (...)
                {
                    prop_name = STR("<prop-name-error>");
                    prop_type = STR("<prop-type-error>");
                }
            }

            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] NativeHookDiagnostics param[{}] name={} type={} flags={} data={} value={}\n"),
                i,
                prop_name,
                prop_type,
                property_flags_summary(prop),
                data,
                describe_param_value(prop, data));
        }

        Unreal::FProperty* return_prop = nullptr;
        try
        {
            return_prop = current_func ? current_func->GetReturnProperty() : nullptr;
        }
        catch (...)
        {
            return_prop = nullptr;
        }

        if (return_prop)
        {
            std::wstring return_name = STR("<return-name-error>");
            std::wstring return_type = STR("<return-type-error>");
            try
            {
                return_name = return_prop->GetName();
                return_type = return_prop->GetClass().GetName();
            }
            catch (...)
            {
            }

            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] NativeHookDiagnostics return name={} type={} flags={} data={} value={}\n"),
                return_name,
                return_type,
                property_flags_summary(return_prop),
                context.RESULT_DECL,
                describe_param_value(return_prop, context.RESULT_DECL));
        }
    }

    static Unreal::UObject* get_native_method_target(
        JSMod::NativeObjectMethodHookData* hook_data,
        Unreal::UnrealScriptFunctionCallableContext& context,
        const std::vector<std::pair<Unreal::FProperty*, void*>>& params)
    {
        if (!hook_data)
        {
            return nullptr;
        }

        if (hook_data->target_context)
        {
            return context.Context;
        }

        const int32_t index = hook_data->target_param_index;
        if (index < 0 || static_cast<size_t>(index) >= params.size())
        {
            return nullptr;
        }

        auto* prop = params[static_cast<size_t>(index)].first;
        void* data = params[static_cast<size_t>(index)].second;
        if (!prop || !data || !prop->IsA<Unreal::FObjectProperty>())
        {
            return nullptr;
        }

        auto** object_ptr = static_cast<Unreal::UObject**>(data);
        return object_ptr ? *object_ptr : nullptr;
    }

    static void execute_native_object_method_hook(
        JSMod::NativeObjectMethodHookData* hook_data,
        Unreal::UnrealScriptFunctionCallableContext& context,
        const wchar_t* phase)
    {
        if (!hook_data || (hook_data->method_name.empty() && !hook_data->diagnose_params))
        {
            return;
        }

        Unreal::UFunction* current_func = safe_get_current_native_function(context);
        if (!current_func) current_func = hook_data->hook_function;
        if (!current_func) return;

        std::vector<std::pair<Unreal::FProperty*, void*>> params;
        bool has_return_value = false;
        if (!safe_extract_hook_params(current_func, context, params, has_return_value))
        {
            return;
        }

        diagnose_native_hook_params(hook_data, context, current_func, params, has_return_value, phase);

        if (hook_data->method_name.empty())
        {
            return;
        }

        Unreal::UObject* target = get_native_method_target(hook_data, context, params);
        if (!target || !full_name_contains(target, hook_data->target_filter))
        {
            log_native_method_hook(hook_data, phase, STR("skip-target"), target);
            return;
        }

        Unreal::UFunction* method = nullptr;
        try
        {
            method = target->GetFunctionByNameInChain(hook_data->method_name.c_str());
        }
        catch (...)
        {
            method = nullptr;
        }
        if (!method)
        {
            log_native_method_hook(hook_data, phase, STR("method-not-found"), target);
            return;
        }

        void* params_memory = nullptr;
        bool process_event_ok = false;
        try
        {
            const int32_t params_size = method->GetParmsSize();
            if (params_size > 0)
            {
                params_memory = calloc(1, params_size);
                if (!params_memory)
                {
                    return;
                }
            }

            size_t arg_index = 0;
            for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(method, Unreal::EFieldIterationFlags::IncludeDeprecated))
            {
                if (!prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm)) continue;
                if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm)) continue;
                if (arg_index >= hook_data->args.size()) break;

                void* prop_addr = prop->ContainerPtrToValuePtr<void>(params_memory);
                fill_native_method_arg(prop, prop_addr, hook_data->args[arg_index]);
                arg_index++;
            }

            process_event_ok = safe_process_event(target, method, params_memory);
            log_native_method_hook(hook_data, phase, process_event_ok ? STR("called") : STR("process-event-failed"), target);
        }
        catch (...)
        {
            log_native_method_hook(hook_data, phase, STR("exception"), target);
        }

        if (params_memory)
        {
            free(params_memory);
        }
    }

    // Common logic for both pre and post hook callbacks
    static int seh_probe_context_object(Unreal::UnrealScriptFunctionCallableContext* context)
    {
        __try
        {
            volatile auto* ctx_obj = context->Context;
            (void)ctx_obj;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void js_ufunction_hook_common(
        Unreal::UnrealScriptFunctionCallableContext& context,
        void* custom_data,
        bool is_pre)
    {
        if (!safe_probe_hook_data(custom_data)) return;

        auto* hook_data = static_cast<JSMod::JSUFunctionHookData*>(custom_data);
        if (hook_data->owner && hook_data->owner->is_subsystem_disabled(JSMod::GuardedSubsystem::Hook))
        {
            return;
        }

        JSValue& callback = is_pre ? hook_data->pre_callback : hook_data->post_callback;
        if (!hook_data->ctx || JS_IsUndefined(callback)) return;
        if (hook_data->is_executing) return;

        if (!seh_probe_context_object(&context))
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] {}-hook: context object probe failed, skipping\n"),
                is_pre ? STR("Pre") : STR("Post"));
            return;
        }

        try
        {
            Unreal::UFunction* current_func = safe_get_current_native_function(context);
            if (!current_func) current_func = hook_data->function;
            if (!current_func) return;

            std::vector<std::pair<Unreal::FProperty*, void*>> params;
            bool has_return_value = false;
            if (!safe_extract_hook_params(current_func, context, params, has_return_value))
            {
                return;
            }
            if (is_pre) hook_data->has_return_value = has_return_value;

            const bool on_event_loop_thread = hook_data->owner && hook_data->owner->is_event_loop_thread();

            bool has_out_params = false;
            for (auto& [prop, data] : params)
            {
                if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_OutParm)
                    && !prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ConstParm))
                {
                    has_out_params = true;
                    break;
                }
            }

            if (!on_event_loop_thread && !hook_data->force_sync)
            {
                if (has_out_params)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.JavaScript] Hook callback with OutParams arrived off event loop thread; degrading to snapshot mode\n"));
                }
                snapshot_hook_params(params, context.Context, hook_data, is_pre);
                return;
            }

            execute_hook_callback_direct(hook_data, context, params, is_pre, on_event_loop_thread);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] {}-hook exception in hook_common: {}\n"),
                is_pre ? STR("Pre") : STR("Post"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            if (hook_data->owner)
                hook_data->owner->report_subsystem_failure(JSMod::GuardedSubsystem::Hook,
                    is_pre ? L"PreHookCommon" : L"PostHookCommon", L"C++ exception");
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] {}-hook unknown exception in hook_common\n"),
                is_pre ? STR("Pre") : STR("Post"));
            if (hook_data->owner)
                hook_data->owner->report_subsystem_failure(JSMod::GuardedSubsystem::Hook,
                    is_pre ? L"PreHookCommon" : L"PostHookCommon", L"Unknown exception");
        }
    }

    static void native_object_method_hook_common(
        Unreal::UnrealScriptFunctionCallableContext& context,
        void* custom_data,
        bool is_pre)
    {
        auto* hook_data = static_cast<JSMod::NativeObjectMethodHookData*>(custom_data);
        if (!hook_data || !hook_data->owner)
        {
            return;
        }
        if (hook_data->owner->is_subsystem_disabled(JSMod::GuardedSubsystem::Hook))
        {
            return;
        }
        if ((is_pre && !hook_data->run_pre) || (!is_pre && !hook_data->run_post))
        {
            return;
        }
        execute_native_object_method_hook(hook_data, context, is_pre ? STR("pre") : STR("post"));
    }

    void JSMod::native_object_method_hook_pre(Unreal::UnrealScriptFunctionCallableContext& context, void* custom_data)
    {
        native_object_method_hook_common(context, custom_data, true);
    }

    void JSMod::native_object_method_hook_post(Unreal::UnrealScriptFunctionCallableContext& context, void* custom_data)
    {
        native_object_method_hook_common(context, custom_data, false);
    }

    // ============================================
    // UFunction Hook Registration
    // ============================================

    auto JSMod::register_bind_hook(JSContext* ctx,
                                   const std::wstring& function_path,
                                   JSValue pre_callback,
                                   JSValue post_callback) -> std::pair<int32_t, int32_t>
    {
        if (function_path.empty())
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] register_bind_hook: function path is empty\n"));
            return {0, 0};
        }

        auto bind_hook = std::make_unique<JSBindHookData>();
        bind_hook->owner = this;
        bind_hook->ctx = ctx;
        bind_hook->function_path = function_path;
        bind_hook->bind_id = m_next_bind_hook_id++;

        if (JS_IsFunction(ctx, pre_callback))
            bind_hook->pre_callback = JS_DupValue(ctx, pre_callback);
        else
            bind_hook->pre_callback = JS_UNDEFINED;

        if (JS_IsFunction(ctx, post_callback))
            bind_hook->post_callback = JS_DupValue(ctx, post_callback);
        else
            bind_hook->post_callback = JS_UNDEFINED;

        JSBindHookData* raw_bind_hook = bind_hook.get();
        {
            std::lock_guard<std::mutex> lock(m_bind_hooks_mutex);
            m_bind_hooks.push_back(std::move(bind_hook));
        }

        const bool bind_watch_ready = ensure_bind_watch_registered(this);
        bool activated_immediately = false;
        if (Unreal::UFunction* function = resolve_function_by_path(function_path))
        {
            activated_immediately = activate_bind_hook_instance(this, *raw_bind_hook, function, false);
        }

        if (!activated_immediately)
        {
            Output::send<LogLevel::Normal>(
                bind_watch_ready
                    ? STR("[UE4SSL.JavaScript] Bind hook registered for {} and waiting for UFunction::Bind\n")
                    : STR("[UE4SSL.JavaScript] Bind hook registered for {} without UFunction::Bind watcher; only immediate activation is available\n"),
                function_path);
        }

        return {raw_bind_hook->bind_id, raw_bind_hook->bind_id};
    }

    auto JSMod::unregister_bind_hook(int32_t first_id, int32_t second_id) -> bool
    {
        std::unique_ptr<JSBindHookData> bind_hook_to_remove{};

        {
            std::lock_guard<std::mutex> lock(m_bind_hooks_mutex);
            for (auto it = m_bind_hooks.begin(); it != m_bind_hooks.end(); ++it)
            {
                const auto& bind_hook = *it;
                if (bind_hook && (bind_hook->bind_id == first_id || bind_hook->bind_id == second_id))
                {
                    bind_hook_to_remove = std::move(*it);
                    m_bind_hooks.erase(it);
                    break;
                }
            }
        }

        if (!bind_hook_to_remove)
        {
            return false;
        }

        deactivate_bind_hook_instance(this, *bind_hook_to_remove);
        free_bind_hook_callbacks(*bind_hook_to_remove);
        return true;
    }

    auto JSMod::process_pending_bind_hook_activations() -> void
    {
        std::vector<PendingBindHookActivation> activations{};
        {
            std::lock_guard<std::mutex> lock(m_pending_bind_hook_activations_mutex);
            activations.swap(m_pending_bind_hook_activations);
        }

        if (activations.empty())
        {
            return;
        }

        std::sort(activations.begin(), activations.end(),
            [](const PendingBindHookActivation& left, const PendingBindHookActivation& right) {
                return left.function_path < right.function_path;
            });
        activations.erase(
            std::unique(activations.begin(), activations.end(),
                [](const PendingBindHookActivation& left, const PendingBindHookActivation& right) {
                    return left.function_path == right.function_path;
                }),
            activations.end());

        std::lock_guard<std::mutex> lock(m_bind_hooks_mutex);
        for (const PendingBindHookActivation& activation : activations)
        {
            Unreal::UFunction* function = resolve_function_by_path(activation.function_path);
            if (!function)
            {
                continue;
            }

            for (auto& bind_hook : m_bind_hooks)
            {
                if (!bind_hook || bind_hook->function_path != activation.function_path)
                {
                    continue;
                }

                activate_bind_hook_instance(this, *bind_hook, function, true);
            }
        }
    }

    auto JSMod::clear_bind_hook_state() -> void
    {
        std::vector<std::unique_ptr<JSBindHookData>> bind_hooks{};
        {
            std::lock_guard<std::mutex> lock(m_bind_hooks_mutex);
            bind_hooks.swap(m_bind_hooks);
        }

        for (auto& bind_hook : bind_hooks)
        {
            if (!bind_hook)
            {
                continue;
            }

            deactivate_bind_hook_instance(this, *bind_hook);
            free_bind_hook_callbacks(*bind_hook);
        }

        {
            std::lock_guard<std::mutex> lock(m_pending_bind_hook_activations_mutex);
            m_pending_bind_hook_activations.clear();
        }

        if (m_bind_watch_callback_id != Unreal::Hook::ERROR_ID)
        {
            try
            {
                Unreal::Hook::UnregisterCallback(m_bind_watch_callback_id);
            }
            catch (...)
            {
            }
            m_bind_watch_callback_id = Unreal::Hook::ERROR_ID;
        }
    }

    auto JSMod::register_ufunction_hook(JSContext* ctx, Unreal::UFunction* function,
                                        JSValue pre_callback, JSValue post_callback, bool force_sync) -> std::pair<int32_t, int32_t>
    {
        if (!function)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] register_ufunction_hook: function is null\n"));
            return {0, 0};
        }

        auto hook_data = std::make_unique<JSUFunctionHookData>();
        hook_data->owner = this;
        hook_data->ctx = ctx;
        hook_data->function = function;
        hook_data->has_return_value = false;
        hook_data->pre_id = 0;
        hook_data->post_id = 0;
        hook_data->force_sync = force_sync;

        if (JS_IsFunction(ctx, pre_callback))
            hook_data->pre_callback = JS_DupValue(ctx, pre_callback);
        else
            hook_data->pre_callback = JS_UNDEFINED;

        if (JS_IsFunction(ctx, post_callback))
            hook_data->post_callback = JS_DupValue(ctx, post_callback);
        else
            hook_data->post_callback = JS_UNDEFINED;

        JSUFunctionHookData* raw_hook_data = hook_data.get();

        {
            std::lock_guard<std::mutex> lock(m_ufunction_hooks_mutex);
            m_ufunction_hooks.push_back(std::move(hook_data));
        }

        auto [generic_pre_id, generic_post_id] = Unreal::UObjectGlobals::RegisterHook(
            function,
            js_ufunction_hook_pre,
            js_ufunction_hook_post,
            raw_hook_data
        );

        raw_hook_data->pre_id = generic_pre_id;
        raw_hook_data->post_id = generic_post_id;

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Registered hook (pre_id={}, post_id={}, sync={}) for {}\n"),
            generic_pre_id, generic_post_id, force_sync ? STR("true") : STR("false"), function->GetFullName());

        return {generic_pre_id, generic_post_id};
    }

    auto JSMod::register_native_object_method_hook(std::unique_ptr<NativeObjectMethodHookData> hook_data) -> std::pair<int32_t, int32_t>
    {
        if (!hook_data || !hook_data->hook_function)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] register_native_object_method_hook: function is null\n"));
            return {0, 0};
        }

        hook_data->owner = this;
        NativeObjectMethodHookData* raw_hook_data = hook_data.get();

        {
            std::lock_guard<std::mutex> lock(m_native_method_hooks_mutex);
            m_native_method_hooks.push_back(std::move(hook_data));
        }

        auto [generic_pre_id, generic_post_id] = Unreal::UObjectGlobals::RegisterHook(
            raw_hook_data->hook_function,
            native_object_method_hook_pre,
            native_object_method_hook_post,
            raw_hook_data
        );

        raw_hook_data->pre_id = generic_pre_id;
        raw_hook_data->post_id = generic_post_id;

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Registered native object method hook (pre_id={}, post_id={}) for {} -> {}\n"),
            generic_pre_id,
            generic_post_id,
            raw_hook_data->hook_function->GetFullName(),
            raw_hook_data->method_name);

        return {generic_pre_id, generic_post_id};
    }

    auto JSMod::unregister_ufunction_hook(Unreal::CallbackId pre_id, Unreal::CallbackId post_id) -> bool
    {
        {
            std::lock_guard<std::mutex> lock(m_ufunction_hooks_mutex);

            for (auto it = m_ufunction_hooks.begin(); it != m_ufunction_hooks.end(); ++it)
            {
                auto& hook = *it;
                if (hook && (hook->pre_id == pre_id || hook->post_id == post_id))
                {
                    JSUFunctionHookData* raw_ptr = hook.get();

                    {
                        std::lock_guard<std::mutex> plock(m_pending_hook_callbacks_mutex);
                        m_pending_hook_callbacks.erase(
                            std::remove_if(m_pending_hook_callbacks.begin(), m_pending_hook_callbacks.end(),
                                [raw_ptr](const PendingHookCallback& p) { return p.hook_data == raw_ptr; }),
                            m_pending_hook_callbacks.end());
                    }

                    if (hook->ctx)
                    {
                        if (!JS_IsUndefined(hook->pre_callback))
                        {
                            JS_FreeValue(hook->ctx, hook->pre_callback);
                            hook->pre_callback = JS_UNDEFINED;
                        }
                        if (!JS_IsUndefined(hook->post_callback))
                        {
                            JS_FreeValue(hook->ctx, hook->post_callback);
                            hook->post_callback = JS_UNDEFINED;
                        }
                    }

                    if (hook->function)
                    {
                        (void)safe_engine_unregister_hook(hook->function, hook->pre_id, hook->post_id);
                        hook->function = nullptr;
                    }

                    m_ufunction_hooks.erase(it);

                    return true;
                }
            }
        }

        {
            std::lock_guard<std::mutex> native_lock(m_native_method_hooks_mutex);
            for (auto it = m_native_method_hooks.begin(); it != m_native_method_hooks.end(); ++it)
            {
                auto& hook = *it;
                if (hook && (hook->pre_id == pre_id || hook->post_id == post_id))
                {
                    if (hook->hook_function)
                    {
                        (void)safe_engine_unregister_hook(hook->hook_function, hook->pre_id, hook->post_id);
                        hook->hook_function = nullptr;
                    }

                    m_native_method_hooks.erase(it);
                    return true;
                }
            }
        }

        return false;
    }

    void JSMod::js_ufunction_hook_pre(Unreal::UnrealScriptFunctionCallableContext& context, void* custom_data)
    {
        js_ufunction_hook_common(context, custom_data, true);
    }

    void JSMod::js_ufunction_hook_post(Unreal::UnrealScriptFunctionCallableContext& context, void* custom_data)
    {
        js_ufunction_hook_common(context, custom_data, false);
    }

    // ============================================
    // Game Thread Dispatcher
    // ============================================

    static int seh_probe_uobject_hook(void* ptr)
    {
        if (!ptr) return 0;
        __try
        {
            auto* obj = static_cast<Unreal::UObject*>(ptr);
            volatile uintptr_t vtable = *reinterpret_cast<volatile uintptr_t*>(ptr);
            (void)vtable;

            int32_t idx = obj->GetInternalIndex();
            if (idx < 0 || idx >= Unreal::FUObjectArray::GetNumElements()) return 0;
            auto* item = Unreal::FUObjectArray::IndexToObject(idx);
            if (!item) return 0;
            if (!item->IsValid(true)) return 0;
            if (item->GetUObject() != obj) return 0;

            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    auto JSMod::setup_game_thread_dispatcher() -> void
    {
        if (m_game_thread_callback_registered) return;

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Setting up game thread dispatcher for RPC calls...\n"));

        Unreal::Hook::FCallbackOptions opts;
        opts.OwnerModName = STR("UE4SSL.JavaScript");
        opts.HookName = STR("GameThreadRpcDispatcher");
        auto callback_id = Unreal::Hook::RegisterProcessEventPreCallback(
            [this](Unreal::Hook::TCallbackIterationData<void>& /*Data*/, Unreal::UObject* /*Context*/, Unreal::UFunction* /*Function*/, void* /*Parms*/) {
                try
                {
                    std::vector<PendingGameThreadCall> to_execute;
                    {
                        std::lock_guard<std::mutex> lock(m_pending_game_thread_mutex);
                        if (m_pending_game_thread_calls.empty()) return;
                        to_execute.swap(m_pending_game_thread_calls);
                    }
                    if (to_execute.size() > 1)
                    {
                        Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Draining {} queued game thread RPC calls\n"),
                            to_execute.size());
                    }

                    for (auto& call : to_execute)
                    {
                        try
                        {
                            if (!seh_probe_uobject_hook(call.object) || !seh_probe_uobject_hook(call.function))
                            {
                                std::wstring function_path = safe_get_function_path(call.function);
                                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Game thread RPC: stale UObject/UFunction pointer, skipping call (function={})\n"),
                                    function_path.empty() ? L"<unknown>" : function_path);
                            }
                            else
                            {
                                std::wstring function_path = safe_get_function_path(call.function);
                                Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Executing queued RPC call on game thread (function={})\n"),
                                    function_path.empty() ? L"<unknown>" : function_path);
                                bool ok = safe_process_event(call.object, call.function, call.params_memory);
                                if (!ok)
                                {
                                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Game thread ProcessEvent CRASHED (SEH 0x{:08X})\n"), Seh::GetLastSehCode());
                                }
                            }
                        }
                        catch (...)
                        {
                            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Game thread RPC single call exception\n"));
                        }
                        for (auto* buf : call.raw_string_buffers) { free(buf); }
                        if (call.params_memory) { free(call.params_memory); }
                    }
                }
                catch (const std::exception& e)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Game thread RPC dispatcher exception: {}\n"),
                        std::wstring(e.what(), e.what() + strlen(e.what())));
                }
                catch (...)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Game thread RPC dispatcher unknown exception\n"));
                }
            },
            opts
        );

        if (callback_id != Unreal::Hook::ERROR_ID)
        {
            m_game_thread_callback_registered = true;
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Game thread dispatcher registered successfully\n"));
        }
        else
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Game thread dispatcher failed to register, net functions will call ProcessEvent directly\n"));
        }
    }

    // ============================================
    // ProcessEvent Watch (safe function interception)
    // ============================================

    constexpr size_t kPeWatchStringBufCap = 512;

    struct PeWatchParam { int type; wchar_t str_buf[kPeWatchStringBufCap]; int str_len; };

    static bool seh_read_fstr_to_buf(void* data, wchar_t* buf, int buf_cap, int* out_len)
    {
        __try
        {
            auto* fstr = static_cast<Unreal::FString*>(data);
            if (!fstr) { *out_len = 0; return true; }
            auto* arr_data = fstr->GetCharArray().GetData();
            int len = fstr->GetCharArray().Num();
            if (len > 0) len--;
            if (len > buf_cap - 1) len = buf_cap - 1;
            if (len > 0 && arr_data) memcpy(buf, arr_data, len * sizeof(wchar_t));
            buf[len] = 0;
            *out_len = len;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { *out_len = 0; return false; }
    }

    auto JSMod::setup_pe_watch_hook() -> void
    {
        if (m_pe_watch_hook_registered) return;

        Unreal::Hook::FCallbackOptions opts;
        opts.OwnerModName = STR("UE4SSL.JavaScript");
        opts.HookName = STR("ProcessEventWatch");
        auto callback_id = Unreal::Hook::RegisterProcessEventPreCallback(
            [this](Unreal::Hook::TCallbackIterationData<void>& /*Data*/,
                   Unreal::UObject* Context, Unreal::UFunction* Function, void* Parms) {
                if (!Function || !Parms) return;

                try
                {
                    std::lock_guard<std::mutex> lock(m_pe_watches_mutex);
                    for (size_t wi = 0; wi < m_pe_watches.size(); wi++)
                    {
                        if (Function != m_pe_watches[wi].cached_func) continue;

                        PeWatchParam raw_params[8];
                        int param_count = 0;
                        for (Unreal::FProperty* prop :
                             Unreal::TFieldRange<Unreal::FProperty>(Function, Unreal::EFieldIterationFlags::IncludeDeprecated))
                        {
                            if (param_count >= 8) break;
                            if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm)) continue;
                            if (!prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm)) continue;
                            void* data = static_cast<uint8_t*>(Parms) + prop->GetOffset_Internal();
                            auto& p = raw_params[param_count];
                            p.type = 0;
                            p.str_len = 0;
                            p.str_buf[0] = 0;

                            if (Unreal::CastField<Unreal::FStrProperty>(prop))
                            {
                                seh_read_fstr_to_buf(data, p.str_buf, static_cast<int>(std::size(p.str_buf)), &p.str_len);
                                p.type = 1;
                            }
                            else if (prop->IsA<Unreal::FTextProperty>())
                            {
                                std::wstring text;
                                if (seh_read_ftext_string(data, text))
                                {
                                    int len = static_cast<int>(text.size());
                                    const int max_len = static_cast<int>(std::size(p.str_buf) - 1);
                                    if (len > max_len) len = max_len;
                                    if (len > 0) memcpy(p.str_buf, text.data(), len * sizeof(wchar_t));
                                    p.str_buf[len] = 0;
                                    p.str_len = len;
                                }
                                p.type = 1;
                            }
                            else if (prop->IsA<Unreal::FNameProperty>())
                            {
                                std::wstring name;
                                if (seh_read_fname_string(data, name))
                                {
                                    int len = static_cast<int>(name.size());
                                    const int max_len = static_cast<int>(std::size(p.str_buf) - 1);
                                    if (len > max_len) len = max_len;
                                    if (len > 0) memcpy(p.str_buf, name.data(), len * sizeof(wchar_t));
                                    p.str_buf[len] = 0;
                                    p.str_len = len;
                                }
                                p.type = 1;
                            }
                            param_count++;
                        }

                        PendingHookCallback pending;
                        pending.hook_data = reinterpret_cast<JSUFunctionHookData*>(
                            static_cast<uintptr_t>(wi) | 0x8000000000000000ULL);
                        pending.is_pre = true;
                        pending.context_object = Context;
                        for (int i = 0; i < param_count; i++)
                        {
                            PendingHookCallbackParam p;
                            p.type = raw_params[i].type == 1 ?
                                PendingHookCallbackParam::Type::String :
                                PendingHookCallbackParam::Type::Unknown;
                            if (raw_params[i].str_len > 0)
                                p.str_val.assign(raw_params[i].str_buf, raw_params[i].str_len);
                            pending.params.push_back(std::move(p));
                        }

                        {
                            std::lock_guard<std::mutex> hlock(m_pending_hook_callbacks_mutex);
                            m_pending_hook_callbacks.push_back(std::move(pending));
                        }
                        break;
                    }
                }
                catch (const std::exception& e)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] PE watch callback exception: {}\n"),
                        std::wstring(e.what(), e.what() + strlen(e.what())));
                }
                catch (...)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] PE watch callback unknown exception\n"));
                }
            },
            opts
        );

        if (callback_id != Unreal::Hook::ERROR_ID)
        {
            m_pe_watch_hook_registered = true;
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] ProcessEvent watch hook registered\n"));
        }
    }

    auto JSMod::register_process_event_watch(JSContext* ctx, const std::wstring& func_name, JSValue callback) -> int
    {
        setup_pe_watch_hook();

        auto* ufunc = Unreal::UObjectGlobals::StaticFindObject<Unreal::UFunction*>(nullptr, nullptr, func_name);
        if (!ufunc)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] ProcessEvent watch: UFunction not found: {}\n"), func_name);
            return -1;
        }

        std::lock_guard<std::mutex> lock(m_pe_watches_mutex);
        int idx = static_cast<int>(m_pe_watches.size());
        m_pe_watches.push_back({func_name, ufunc, JS_DupValue(ctx, callback)});
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] ProcessEvent watch added: {} func={:016X} (idx={})\n"),
            func_name, std::bit_cast<uintptr_t>(ufunc), idx);
        return idx;
    }

    // ============================================
    // Key Binding Registration
    // ============================================

    auto JSMod::register_key_bind(JSContext* ctx, uint8_t key, JSValue callback,
                                  bool with_ctrl, bool with_shift, bool with_alt) -> bool
    {
        auto key_bind = std::make_unique<KeyBindCallback>();
        key_bind->owner = this;
        key_bind->ctx = ctx;
        key_bind->callback = JS_DupValue(ctx, callback);
        key_bind->key = key;
        key_bind->with_ctrl = with_ctrl;
        key_bind->with_shift = with_shift;
        key_bind->with_alt = with_alt;

        KeyBindCallback* raw_key_bind = key_bind.get();

        {
            std::lock_guard<std::mutex> lock(m_key_bindings_mutex);
            m_key_bindings.push_back(std::move(key_bind));
        }

        Input::Handler::ModifierKeyArray modifier_keys{};
        size_t mod_idx = 0;
        if (with_ctrl) modifier_keys[mod_idx++] = Input::ModifierKey::CONTROL;
        if (with_shift) modifier_keys[mod_idx++] = Input::ModifierKey::SHIFT;
        if (with_alt) modifier_keys[mod_idx++] = Input::ModifierKey::ALT;

        auto& program = UE4SSProgram::get_program();

        auto queue_keybind_callback = [this](KeyBindCallback* kb) {
            if (!kb || !kb->ctx) return;
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] [DIAG] KeyBind QUEUED for key 0x{:X} (input thread -> pending queue)\n"), kb->key);
            std::lock_guard<std::mutex> lock(m_pending_keybind_mutex);
            m_pending_keybind_callbacks.push_back(kb);
        };

        constexpr uint8_t js_keybind_custom_data = 3;

        if (with_ctrl || with_shift || with_alt)
        {
            program.register_keydown_event_owned(
                static_cast<Input::Key>(key),
                modifier_keys,
                [raw_key_bind, queue_keybind_callback]() {
                    queue_keybind_callback(raw_key_bind);
                },
                js_keybind_custom_data, reinterpret_cast<uintptr_t>(this)
            );
        }
        else
        {
            program.register_keydown_event_owned(
                static_cast<Input::Key>(key),
                [raw_key_bind, queue_keybind_callback]() {
                    queue_keybind_callback(raw_key_bind);
                },
                js_keybind_custom_data, reinterpret_cast<uintptr_t>(this)
            );
        }

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Registered key bind for key 0x{:X} (ctrl={}, shift={}, alt={})\n"),
            key, with_ctrl, with_shift, with_alt);

        return true;
    }

} // namespace RC::JSScript
