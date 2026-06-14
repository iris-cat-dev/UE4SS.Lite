#include "JSInternal.hpp"

#include <string>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FStructProperty.hpp>

namespace RC::JSScript
{
    // SEH-isolated property lookup on UObject
    static int seh_find_property(Unreal::UObject* obj, const wchar_t* name,
                                 Unreal::FProperty** out_prop, void** out_data)
    {
        *out_prop = nullptr;
        *out_data = nullptr;
        __try
        {
            Unreal::FProperty* prop = obj->GetPropertyByNameInChain(Unreal::FName(name, Unreal::FNAME_Find));
            if (!prop) return 0;
            void* data = prop->ContainerPtrToValuePtr<void>(obj);
            *out_prop = prop;
            *out_data = data;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static std::vector<std::wstring> split_property_path(const std::wstring& path)
    {
        std::vector<std::wstring> parts;
        size_t start = 0;
        while (start < path.size())
        {
            const size_t dot = path.find(L'.', start);
            if (dot == std::wstring::npos)
            {
                if (start < path.size())
                {
                    parts.push_back(path.substr(start));
                }
                break;
            }
            parts.push_back(path.substr(start, dot - start));
            start = dot + 1;
        }
        return parts;
    }

    static int seh_find_struct_field(Unreal::UScriptStruct* script_struct, void* struct_data, const wchar_t* seg_w,
                                     Unreal::FProperty** out_prop, void** out_data)
    {
        *out_prop = nullptr;
        *out_data = nullptr;
        if (!script_struct || !struct_data || !seg_w)
        {
            return 0;
        }
        __try
        {
            const Unreal::FName want(seg_w, Unreal::FNAME_Find);
            for (Unreal::FProperty* field : Unreal::TFieldRange<Unreal::FProperty>(
                     script_struct, Unreal::EFieldIterationFlags::IncludeDeprecated))
            {
                if (!(field->GetFName() == want))
                {
                    continue;
                }
                void* field_data = field->ContainerPtrToValuePtr<void>(struct_data);
                *out_prop = field;
                *out_data = field_data;
                return 1;
            }
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int seh_resolve_property_path(Unreal::UObject* root, const std::vector<std::wstring>& parts,
                                         Unreal::FProperty** out_leaf_prop, void** out_leaf_data)
    {
        *out_leaf_prop = nullptr;
        *out_leaf_data = nullptr;
        if (!root || parts.empty())
        {
            return 0;
        }

        Unreal::UObject* current_uobj = root;
        void* struct_base = nullptr;
        Unreal::UScriptStruct* in_struct = nullptr;

        for (size_t i = 0; i < parts.size(); ++i)
        {
            const std::wstring& seg = parts[i];
            if (seg.empty())
            {
                return 0;
            }
            const bool is_last = (i + 1 == parts.size());

            Unreal::FProperty* prop = nullptr;
            void* data = nullptr;

            if (in_struct == nullptr)
            {
                if (!seh_find_property(current_uobj, seg.c_str(), &prop, &data))
                {
                    return 0;
                }
            }
            else
            {
                if (!seh_find_struct_field(in_struct, struct_base, seg.c_str(), &prop, &data))
                {
                    return 0;
                }
            }

            if (is_last)
            {
                *out_leaf_prop = prop;
                *out_leaf_data = data;
                return 1;
            }

            if (!prop->IsA<Unreal::FStructProperty>())
            {
                return 0;
            }
            auto* struct_prop = static_cast<Unreal::FStructProperty*>(prop);
            Unreal::UScriptStruct* next_struct = struct_prop->GetStruct();
            if (!next_struct)
            {
                return 0;
            }

            in_struct = next_struct;
            struct_base = data;
            current_uobj = nullptr;
        }
        return 0;
    }

    JSValue js_get_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "GetProperty requires 2 arguments: uobject, propertyName");

        void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!obj_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject");
        Unreal::UObject* object = static_cast<Unreal::UObject*>(obj_ptr);

        const char* prop_name = JS_ToCString(ctx, argv[1]);
        if (!prop_name)
            return JS_ThrowTypeError(ctx, "Second argument must be a property name string");

        std::wstring wide_name = utf8_to_wide(std::string(prop_name));
        JS_FreeCString(ctx, prop_name);

        try
        {
            const std::vector<std::wstring> parts = split_property_path(wide_name);
            if (parts.empty())
            {
                return JS_NULL;
            }

            Unreal::FProperty* prop = nullptr;
            void* data = nullptr;
            if (!seh_resolve_property_path(object, parts, &prop, &data))
            {
                return JS_NULL;
            }
            if (!prop || !data)
            {
                return JS_NULL;
            }

            return property_to_jsvalue(ctx, prop, data);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] GetProperty exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NULL;
        }
        catch (...)
        {
            return JS_NULL;
        }
    }

    JSValue js_set_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 3)
            return JS_ThrowTypeError(ctx, "SetProperty requires 3 arguments: uobject, propertyName, value");

        void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!obj_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject");
        Unreal::UObject* object = static_cast<Unreal::UObject*>(obj_ptr);

        const char* prop_name = JS_ToCString(ctx, argv[1]);
        if (!prop_name)
            return JS_ThrowTypeError(ctx, "Second argument must be a property name string");

        std::wstring wide_name = utf8_to_wide(std::string(prop_name));
        JS_FreeCString(ctx, prop_name);

        try
        {
            const std::vector<std::wstring> parts = split_property_path(wide_name);
            if (parts.empty())
            {
                return JS_NewBool(ctx, false);
            }

            Unreal::FProperty* prop = nullptr;
            void* data = nullptr;
            if (!seh_resolve_property_path(object, parts, &prop, &data))
            {
                return JS_NewBool(ctx, false);
            }
            if (!prop || !data)
            {
                return JS_NewBool(ctx, false);
            }

            jsvalue_to_property(ctx, prop, data, argv[2]);
            return JS_NewBool(ctx, true);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] SetProperty exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NewBool(ctx, false);
        }
        catch (...)
        {
            return JS_NewBool(ctx, false);
        }
    }

    static void export_property_text_inner(Unreal::FProperty* prop, void* data, Unreal::UObject* owner, std::wstring& out_text)
    {
        out_text.clear();
        if (!prop || !data)
        {
            return;
        }

        Unreal::FString exported{};
        prop->ExportTextItem(exported, data, data, owner, 0);

        const int32_t num = exported.GetCharArray().Num();
        if (num <= 1)
        {
            return;
        }

        const wchar_t* chars = exported.GetCharArray().GetData();
        if (!chars)
        {
            return;
        }

        out_text.assign(chars, num - 1);
    }

    static int seh_export_property_text(Unreal::FProperty* prop, void* data, Unreal::UObject* owner, std::wstring& out_text)
    {
        out_text.clear();
        if (!prop || !data)
        {
            return 0;
        }

        __try
        {
            export_property_text_inner(prop, data, owner, out_text);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out_text.clear();
            return 0;
        }
    }

    JSValue js_export_property_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "ExportPropertyText requires 2 arguments: uobject, propertyName");

        void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!obj_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject");
        Unreal::UObject* object = static_cast<Unreal::UObject*>(obj_ptr);

        const char* prop_name = JS_ToCString(ctx, argv[1]);
        if (!prop_name)
            return JS_ThrowTypeError(ctx, "Second argument must be a property name string");

        std::wstring wide_name = utf8_to_wide(std::string(prop_name));
        JS_FreeCString(ctx, prop_name);

        try
        {
            const std::vector<std::wstring> parts = split_property_path(wide_name);
            if (parts.empty())
            {
                return JS_NULL;
            }

            Unreal::FProperty* prop = nullptr;
            void* data = nullptr;
            if (!seh_resolve_property_path(object, parts, &prop, &data) || !prop || !data)
            {
                return JS_NULL;
            }

            std::wstring exported{};
            if (!seh_export_property_text(prop, data, object, exported))
            {
                return JS_NULL;
            }

            return JS_NewString(ctx, wide_to_utf8(exported).c_str());
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] ExportPropertyText exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NULL;
        }
        catch (...)
        {
            return JS_NULL;
        }
    }

    JSValue js_get_property_path(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        return js_get_property(ctx, this_val, argc, argv);
    }

    JSValue js_set_property_path(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        return js_set_property(ctx, this_val, argc, argv);
    }

    JSValue js_apply_object_patch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "ApplyObjectPatch requires 2 arguments: uobject, patchObject");

        void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!obj_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject");
        if (!JS_IsObject(argv[1]))
            return JS_ThrowTypeError(ctx, "Second argument must be an object");

        uint32_t applied = 0;
        uint32_t failed = 0;
        JSPropertyEnum* names = nullptr;
        uint32_t len = 0;

        if (JS_GetOwnPropertyNames(ctx, &names, &len, argv[1], JS_GPN_STRING_MASK) != 0)
        {
            return JS_ThrowInternalError(ctx, "ApplyObjectPatch could not enumerate patch object");
        }

        for (uint32_t i = 0; i < len; i++)
        {
            const char* key = JS_AtomToCString(ctx, names[i].atom);
            JSValue value = JS_GetProperty(ctx, argv[1], names[i].atom);
            if (!key || JS_IsUndefined(value))
            {
                if (key) JS_FreeCString(ctx, key);
                JS_FreeValue(ctx, value);
                continue;
            }

            JSValue path = JS_NewString(ctx, key);
            JSValueConst set_args[3] = { argv[0], path, value };
            JSValue set_result = js_set_property(ctx, this_val, 3, set_args);
            if (JS_ToBool(ctx, set_result))
                applied++;
            else
                failed++;

            JS_FreeValue(ctx, set_result);
            JS_FreeValue(ctx, path);
            JS_FreeValue(ctx, value);
            JS_FreeCString(ctx, key);
        }

        js_free(ctx, names);

        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "applied", JS_NewUint32(ctx, applied));
        JS_SetPropertyStr(ctx, result, "failed", JS_NewUint32(ctx, failed));
        JS_SetPropertyStr(ctx, result, "__success", JS_NewBool(ctx, failed == 0));
        return result;
    }

} // namespace RC::JSScript
