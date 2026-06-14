#include "JSType/JSUObject.hpp"

#define NOMINMAX
#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

extern "C" {
#include "quickjs.h"
}

namespace RC::JSScript
{
    // Static class ID
    JSClassID JSUObject::class_id = 0;

    // UObject data stored in opaque pointer
    struct UObjectData
    {
        Unreal::UObject* object;
        bool prevent_gc;  // Prevent GC from collecting this object
    };

    static int seh_clear_root_set(Unreal::UObject* obj)
    {
        __try
        {
            obj->ClearRootSet();
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void js_uobject_finalizer(JSRuntime* rt, JSValue val)
    {
        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque(val, JSUObject::class_id));
        if (data)
        {
            if (data->prevent_gc && data->object)
            {
                seh_clear_root_set(data->object);
            }
            js_free_rt(rt, data);
        }
    }

    // SEH probe: verify the UObject pointer is still readable (catches stale/GC'd objects)
    static int seh_probe_uobject(void* obj)
    {
        if (!obj) return 0;
        __try
        {
            volatile uintptr_t vtable = *reinterpret_cast<volatile uintptr_t*>(obj);
            (void)vtable;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static auto wide_to_utf8_local(const std::wstring& wide) -> std::string
    {
        if (wide.empty())
        {
            return {};
        }

        const int utf8_len = WideCharToMultiByte(
            CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (utf8_len <= 0)
        {
            return {};
        }

        std::string out(static_cast<size_t>(utf8_len), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), utf8_len, nullptr, nullptr);
        return out;
    }

    // Get UObject's full name
    static JSValue js_uobject_get_full_name(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque2(ctx, this_val, JSUObject::class_id));
        if (!data || !data->object)
            return JS_ThrowTypeError(ctx, "Invalid UObject");
        if (!seh_probe_uobject(data->object))
            return JS_ThrowInternalError(ctx, "UObject pointer is stale (access violation)");

        try
        {
            std::wstring full_name = data->object->GetFullName();
            std::string utf8_name = wide_to_utf8_local(full_name);
            return JS_NewString(ctx, utf8_name.c_str());
        }
        catch (const std::exception& e)
        {
            return JS_ThrowInternalError(ctx, "GetFullName: %s", e.what());
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "GetFullName: unknown exception");
        }
    }

    // Get UObject's class
    static JSValue js_uobject_get_class(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque2(ctx, this_val, JSUObject::class_id));
        if (!data || !data->object)
            return JS_ThrowTypeError(ctx, "Invalid UObject");
        if (!seh_probe_uobject(data->object))
            return JS_ThrowInternalError(ctx, "UObject pointer is stale (access violation)");

        try
        {
            Unreal::UClass* obj_class = data->object->GetClassPrivate();
            if (!obj_class) return JS_NULL;
            return JSUObject::create(ctx, obj_class);
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "GetClass: exception during access");
        }
    }

    // Check if object is instance of a class
    static JSValue js_uobject_is_a(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
            return JS_ThrowTypeError(ctx, "IsA requires a class name argument");

        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque2(ctx, this_val, JSUObject::class_id));
        if (!data || !data->object)
            return JS_ThrowTypeError(ctx, "Invalid UObject");
        if (!seh_probe_uobject(data->object))
            return JS_ThrowInternalError(ctx, "UObject pointer is stale (access violation)");

        const char* class_name = JS_ToCString(ctx, argv[0]);
        if (!class_name)
            return JS_ThrowTypeError(ctx, "Invalid class name");

        std::wstring wide_name(class_name, class_name + strlen(class_name));
        JS_FreeCString(ctx, class_name);

        try
        {
            Unreal::UClass* obj_class = data->object->GetClassPrivate();
            if (obj_class)
            {
                std::wstring obj_class_name = obj_class->GetName();
                if (obj_class_name.find(wide_name) != std::wstring::npos)
                    return JS_TRUE;
            }
            return JS_FALSE;
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "IsA: exception during class check");
        }
    }

    // Get object's memory address (for debugging)
    static JSValue js_uobject_get_address(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque2(ctx, this_val, JSUObject::class_id));
        if (!data || !data->object)
        {
            return JS_ThrowTypeError(ctx, "Invalid UObject");
        }

        return JS_NewInt64(ctx, reinterpret_cast<int64_t>(data->object));
    }

    // Check if object is valid
    static JSValue js_uobject_is_valid(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque2(ctx, this_val, JSUObject::class_id));
        if (!data || !data->object)
            return JS_FALSE;
        return JS_NewBool(ctx, seh_probe_uobject(data->object));
    }

    // Get object name
    static JSValue js_uobject_get_name(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque2(ctx, this_val, JSUObject::class_id));
        if (!data || !data->object)
            return JS_ThrowTypeError(ctx, "Invalid UObject");
        if (!seh_probe_uobject(data->object))
            return JS_ThrowInternalError(ctx, "UObject pointer is stale (access violation)");

        try
        {
            std::wstring name = data->object->GetName();
            std::string utf8_name = wide_to_utf8_local(name);
            return JS_NewString(ctx, utf8_name.c_str());
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "GetName: exception during access");
        }
    }

    // Dynamic property getter (exotic object method)
    static int js_uobject_get_own_property(JSContext* ctx, JSPropertyDescriptor* desc,
                                           JSValueConst obj, JSAtom prop)
    {
        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque(obj, JSUObject::class_id));
        if (!data || !data->object) return 0;
        if (!seh_probe_uobject(data->object)) return 0;

        const char* prop_name = JS_AtomToCString(ctx, prop);
        if (!prop_name) return 0;

        std::wstring wide_prop_name(prop_name, prop_name + strlen(prop_name));
        JS_FreeCString(ctx, prop_name);

        try
        {
            Unreal::UClass* obj_class = data->object->GetClassPrivate();
            if (!obj_class) return 0;
            return 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    static JSValue js_uobject_add_to_root(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque2(ctx, this_val, JSUObject::class_id));
        if (!data || !data->object)
            return JS_ThrowTypeError(ctx, "Invalid UObject");
        if (!seh_probe_uobject(data->object))
            return JS_ThrowInternalError(ctx, "UObject pointer is stale");

        __try
        {
            data->object->SetRootSet();
            data->prevent_gc = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return JS_ThrowInternalError(ctx, "AddToRoot: SEH exception");
        }
        return JS_UNDEFINED;
    }

    static JSValue js_uobject_remove_from_root(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque2(ctx, this_val, JSUObject::class_id));
        if (!data || !data->object)
            return JS_ThrowTypeError(ctx, "Invalid UObject");
        if (!seh_probe_uobject(data->object))
            return JS_ThrowInternalError(ctx, "UObject pointer is stale");

        if (data->prevent_gc)
        {
            seh_clear_root_set(data->object);
            data->prevent_gc = false;
        }
        return JS_UNDEFINED;
    }

    // Prototype function list
    static const JSCFunctionListEntry js_uobject_proto_funcs[] = {
        JS_CFUNC_DEF("GetFullName", 0, js_uobject_get_full_name),
        JS_CFUNC_DEF("GetClass", 0, js_uobject_get_class),
        JS_CFUNC_DEF("IsA", 1, js_uobject_is_a),
        JS_CFUNC_DEF("GetAddress", 0, js_uobject_get_address),
        JS_CFUNC_DEF("IsValid", 0, js_uobject_is_valid),
        JS_CFUNC_DEF("GetName", 0, js_uobject_get_name),
        JS_CFUNC_DEF("AddToRoot", 0, js_uobject_add_to_root),
        JS_CFUNC_DEF("RemoveFromRoot", 0, js_uobject_remove_from_root),
    };

    // Class definition
    static JSClassDef js_uobject_class_def = {
        .class_name = "UObject",
        .finalizer = js_uobject_finalizer,
        .gc_mark = nullptr,
        .call = nullptr,
        .exotic = nullptr,
    };

    auto JSUObject::init_class(JSContext* ctx) -> void
    {
        JSRuntime* rt = JS_GetRuntime(ctx);

        // Create class ID if not already created
        if (class_id == 0)
        {
            JS_NewClassID(rt, &class_id);
        }

        // Create the class
        JS_NewClass(rt, class_id, &js_uobject_class_def);

        // Create prototype
        JSValue proto = JS_NewObject(ctx);
        JS_SetPropertyFunctionList(ctx, proto, js_uobject_proto_funcs, 
            sizeof(js_uobject_proto_funcs) / sizeof(js_uobject_proto_funcs[0]));

        // Set class prototype
        JS_SetClassProto(ctx, class_id, proto);

        Output::send<LogLevel::Normal>(STR("[JSScript] UObject class initialized\n"));
    }

    auto JSUObject::create(JSContext* ctx, void* uobject) -> JSValue
    {
        if (!uobject)
        {
            return JS_NULL;
        }

        // Allocate data
        UObjectData* data = static_cast<UObjectData*>(js_malloc(ctx, sizeof(UObjectData)));
        if (!data)
        {
            return JS_EXCEPTION;
        }

        data->object = static_cast<Unreal::UObject*>(uobject);
        data->prevent_gc = false;

        // Create object with class
        JSValue obj = JS_NewObjectClass(ctx, class_id);
        if (JS_IsException(obj))
        {
            js_free(ctx, data);
            return obj;
        }

        JS_SetOpaque(obj, data);
        return obj;
    }

    auto JSUObject::get_uobject(JSContext* ctx, JSValue val) -> void*
    {
        UObjectData* data = static_cast<UObjectData*>(JS_GetOpaque2(ctx, val, class_id));
        if (!data || !data->object)
        {
            return nullptr;
        }
        if (!seh_probe_uobject(data->object))
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] get_uobject: pointer {:016X} is stale, returning null\n"),
                reinterpret_cast<uintptr_t>(data->object));
            data->object = nullptr;
            return nullptr;
        }
        return data->object;
    }

} // namespace RC::JSScript
