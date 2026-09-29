#include "JSInternal.hpp"
#include "JSGameThreadDispatcher.hpp"

#include <chrono>
#include <memory>

#define NOMINMAX
#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/Hooks.hpp>

namespace RC::JSScript
{
    // ============================================
    // GameThreadDispatcher core
    // ============================================

    auto GameThreadDispatcher::dispatch_sync(std::function<void*()> op) -> void*
    {
        auto t_start = std::chrono::steady_clock::now();

        PendingOp pending;
        pending.operation = std::move(op);
        pending.is_sync = true;
        auto future = pending.result_promise.get_future();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending_ops.push_back(std::move(pending));
        }

        auto status = future.wait_for(std::chrono::seconds(15));
        if (status == std::future_status::timeout)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] GameThreadDispatcher: dispatch_sync timed out after 15s, returning nullptr\n"));
            return nullptr;
        }

        double wait_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start).count();
        if (wait_ms > 50.0)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] [PERF] dispatch_sync waited {:.1f}ms for game thread\n"), wait_ms);
        }

        try
        {
            return future.get();
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] GameThreadDispatcher: dispatch_sync future.get() exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return nullptr;
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] GameThreadDispatcher: dispatch_sync future.get() unknown exception\n"));
            return nullptr;
        }
    }

    auto GameThreadDispatcher::dispatch_async(std::function<void()> op) -> void
    {
        PendingOp pending;
        pending.operation = [fn = std::move(op)]() -> void* { fn(); return nullptr; };
        pending.is_sync = false;

        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending_ops.push_back(std::move(pending));
    }

    static int seh_execute_dispatch(std::function<void*()>* op_ptr, void** out_result)
    {
        *out_result = nullptr;
        __try
        {
            *out_result = (*op_ptr)();
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int seh_probe_uobject_safe(void* ptr)
    {
        if (!ptr) return 0;
        __try
        {
            volatile uintptr_t vtable = *reinterpret_cast<volatile uintptr_t*>(ptr);
            (void)vtable;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int seh_drain_umg_dispatcher(GameThreadDispatcher* dispatcher)
    {
        __try
        {
            dispatcher->drain_on_game_thread();
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    auto GameThreadDispatcher::drain_on_game_thread() -> void
    {
        std::vector<PendingOp> to_execute;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pending_ops.empty()) return;
            to_execute.swap(m_pending_ops);
        }

        for (auto& op : to_execute)
        {
            void* result = nullptr;
            if (!seh_execute_dispatch(&op.operation, &result))
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] GameThreadDispatcher: SEH exception during operation\n"));
            }

            if (op.is_sync)
            {
                try
                {
                    op.result_promise.set_value(result);
                }
                catch (const std::exception& e)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] GameThreadDispatcher: exception setting promise: {}\n"),
                        std::wstring(e.what(), e.what() + strlen(e.what())));
                }
                catch (...)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] GameThreadDispatcher: unknown exception setting promise\n"));
                }
            }
        }
    }

    auto GameThreadDispatcher::has_pending() const -> bool
    {
        return !m_pending_ops.empty();
    }

    // ============================================
    // UMG dispatcher setup (hooks into ProcessEvent pre-callback)
    // ============================================

    auto JSMod::setup_umg_dispatcher() -> void
    {
        if (m_umg_dispatcher_registered) return;

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Setting up UMG game thread dispatcher...\n"));

        Unreal::Hook::FCallbackOptions opts;
        opts.OwnerModName = STR("UE4SSL.JavaScript");
        opts.HookName = STR("UMGGameThreadDispatcher");
        auto callback_id = Unreal::Hook::RegisterProcessEventPreCallback(
            [this](Unreal::Hook::TCallbackIterationData<void>&, Unreal::UObject*, Unreal::UFunction*, void*) {
                if (!seh_drain_umg_dispatcher(&m_umg_dispatcher))
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] UMG dispatcher: SEH exception escaped drain_on_game_thread\n"));
                }
            },
            opts
        );

        if (callback_id != Unreal::Hook::ERROR_ID)
        {
            m_umg_dispatcher_registered = true;
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] UMG dispatcher registered successfully\n"));
        }
        else
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] UMG dispatcher failed to register\n"));
        }
    }

    // ============================================
    // JS: NewUObject(classPath_or_classObj, outerObj [, name [, flags]])
    // ============================================

    static int seh_construct_object(Unreal::UClass* cls, Unreal::UObject* outer, Unreal::UObject** out_result)
    {
        *out_result = nullptr;
        __try
        {
            Unreal::FStaticConstructObjectParameters params(cls, outer);
            params.SetFlags = Unreal::EObjectFlags::RF_Transient;
            *out_result = Unreal::UObjectGlobals::StaticConstructObject(params);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int seh_construct_object_from_template(
        Unreal::UClass* cls,
        Unreal::UObject* outer,
        Unreal::UObject* template_object,
        Unreal::UObject** out_result)
    {
        *out_result = nullptr;
        __try
        {
            Unreal::FStaticConstructObjectParameters params(cls, outer);
            params.SetFlags = Unreal::EObjectFlags::RF_Transient;
            params.Template = template_object;
            params.bCopyTransientsFromClassDefaults = false;
            params.bAssumeTemplateIsArchetype = false;
            *out_result = Unreal::UObjectGlobals::StaticConstructObject(params);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static auto make_dispatch_success_token(void* params_memory) -> void*
    {
        return params_memory ? params_memory : reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    }

    static Unreal::UObject* find_first_object_of(const wchar_t* class_name)
    {
        if (!class_name)
        {
            return nullptr;
        }

        __try
        {
            return Unreal::UObjectGlobals::FindFirstInstanceOfClass(class_name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static Unreal::UObject* resolve_widget_blueprint_library_cdo()
    {
        __try
        {
            if (auto* cdo = Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(
                    nullptr, nullptr, STR("/Script/UMG.Default__WidgetBlueprintLibrary")))
            {
                return cdo;
            }

            auto* class_object = Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(
                nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            auto* library_class = class_object ? static_cast<Unreal::UClass*>(class_object) : nullptr;
            if (!library_class)
            {
                return nullptr;
            }

            Unreal::UObject* cdo = library_class->GetClassDefaultObject();
            if (cdo)
            {
                return cdo;
            }

            return library_class->CreateDefaultObject();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool set_function_object_param_by_name(
        Unreal::UFunction* function,
        void* params_memory,
        const wchar_t* property_name,
        Unreal::UObject* value)
    {
        if (!function || !params_memory || !property_name)
        {
            return false;
        }

        for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function, Unreal::EFieldIterationFlags::IncludeDeprecated))
        {
            if (!prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm) ||
                prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
            {
                continue;
            }

            if (prop->GetName() != property_name)
            {
                continue;
            }

            if (!prop->IsA<Unreal::FObjectPropertyBase>())
            {
                return false;
            }

            auto* object_prop = static_cast<Unreal::FObjectPropertyBase*>(prop);
            object_prop->SetObjectPropertyValue(prop->ContainerPtrToValuePtr<void>(params_memory), value);
            return true;
        }

        return false;
    }

    static Unreal::UObject* get_function_return_object(Unreal::UFunction* function, void* params_memory)
    {
        if (!function || !params_memory)
        {
            return nullptr;
        }

        for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function, Unreal::EFieldIterationFlags::IncludeDeprecated))
        {
            if (!prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
            {
                continue;
            }

            if (!prop->IsA<Unreal::FObjectPropertyBase>())
            {
                return nullptr;
            }

            auto* object_prop = static_cast<Unreal::FObjectPropertyBase*>(prop);
            return object_prop->GetObjectPropertyValue(prop->ContainerPtrToValuePtr<void>(params_memory));
        }

        return nullptr;
    }

    static bool ensure_umg_dispatcher(JSMod* mod)
    {
        if (!mod)
        {
            return false;
        }

        if (!mod->m_umg_dispatcher_registered)
        {
            mod->setup_umg_dispatcher();
        }

        return mod->m_umg_dispatcher_registered;
    }

    static Unreal::UClass* resolve_uclass_argument(JSContext* ctx, JSValueConst value, const char* error_prefix)
    {
        if (JS_IsString(value))
        {
            const char* class_path = JS_ToCString(ctx, value);
            if (!class_path)
            {
                JS_ThrowTypeError(ctx, "%s: invalid class path string", error_prefix);
                return nullptr;
            }

            std::wstring wide_path = utf8_to_wide(class_path);
            JS_FreeCString(ctx, class_path);

            auto* found = Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, wide_path);
            if (!found)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] {}: class not found: {}\n"),
                    utf8_to_wide(error_prefix), wide_path);
                JS_ThrowReferenceError(ctx, "%s: class not found", error_prefix);
                return nullptr;
            }

            return static_cast<Unreal::UClass*>(found);
        }

        void* cls_ptr = JSUObject::get_uobject(ctx, value);
        if (!cls_ptr)
        {
            JS_ThrowTypeError(ctx, "%s: expected widget class path string or UClass object", error_prefix);
            return nullptr;
        }
        if (!seh_probe_uobject_safe(cls_ptr))
        {
            JS_ThrowInternalError(ctx, "%s: UClass pointer is stale", error_prefix);
            return nullptr;
        }

        return static_cast<Unreal::UClass*>(cls_ptr);
    }

    JSValue js_new_uobject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "NewUObject requires at least 2 arguments: class (string or UObject), outer");

        JSMod* mod = get_js_mod(ctx);
        if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");

        try
        {
            Unreal::UClass* widget_class = nullptr;

            if (JS_IsString(argv[0]))
            {
                const char* class_path = JS_ToCString(ctx, argv[0]);
                if (!class_path)
                    return JS_ThrowTypeError(ctx, "Invalid class path string");

                std::string utf8_path(class_path);
                std::wstring wide_path = utf8_to_wide(utf8_path);
                JS_FreeCString(ctx, class_path);

                auto* found = Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(
                    nullptr, nullptr, wide_path);
                if (!found)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] NewUObject: class not found: {}\n"), wide_path);
                    return JS_ThrowReferenceError(ctx, "Class not found");
                }
                widget_class = static_cast<Unreal::UClass*>(found);
            }
            else
            {
                void* cls_ptr = JSUObject::get_uobject(ctx, argv[0]);
                if (!cls_ptr)
                    return JS_ThrowTypeError(ctx, "First argument must be a class path string or UClass object");
                if (!seh_probe_uobject_safe(cls_ptr))
                    return JS_ThrowInternalError(ctx, "NewUObject: UClass pointer is stale");
                widget_class = static_cast<Unreal::UClass*>(cls_ptr);
            }

            void* outer_ptr = JSUObject::get_uobject(ctx, argv[1]);
            if (outer_ptr && !seh_probe_uobject_safe(outer_ptr))
                return JS_ThrowInternalError(ctx, "NewUObject: outer UObject pointer is stale");
            Unreal::UObject* outer = outer_ptr ? static_cast<Unreal::UObject*>(outer_ptr) : nullptr;

            if (!mod->m_umg_dispatcher_registered)
                mod->setup_umg_dispatcher();

            if (!mod->m_umg_dispatcher_registered)
            {
                Unreal::UObject* new_obj = nullptr;
                if (!seh_construct_object(widget_class, outer, &new_obj) || !new_obj)
                    return JS_ThrowInternalError(ctx, "StaticConstructObject_Internal failed (SEH or null result)");
                return JSUObject::create(ctx, new_obj);
            }

            Unreal::UClass* cls_capture = widget_class;
            Unreal::UObject* outer_capture = outer;

            void* result = mod->m_umg_dispatcher.dispatch_sync([cls_capture, outer_capture]() -> void* {
                if (!seh_probe_uobject_safe(cls_capture))
                    return nullptr;
                Unreal::UObject* new_obj = nullptr;
                seh_construct_object(cls_capture, outer_capture, &new_obj);
                return static_cast<void*>(new_obj);
            });

            if (!result)
                return JS_ThrowInternalError(ctx, "StaticConstructObject_Internal failed on game thread");

            return JSUObject::create(ctx, result);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] NewUObject exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "NewUObject failed due to exception");
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] NewUObject unknown exception\n"));
            return JS_ThrowInternalError(ctx, "NewUObject failed due to unknown exception");
        }
    }

    JSValue js_umg_create_user_widget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;
        if (argc < 1)
        {
            return JS_ThrowTypeError(ctx, "__umgCreateUserWidget requires: widgetClass [, worldContextObject [, owningPlayer]]");
        }

        JSMod* mod = get_js_mod(ctx);
        if (!mod)
        {
            return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
        }

        Unreal::UClass* widget_class = resolve_uclass_argument(ctx, argv[0], "__umgCreateUserWidget");
        if (!widget_class)
        {
            return JS_EXCEPTION;
        }

        void* world_context_ptr = argc >= 2 ? JSUObject::get_uobject(ctx, argv[1]) : nullptr;
        if (world_context_ptr && !seh_probe_uobject_safe(world_context_ptr))
        {
            return JS_ThrowInternalError(ctx, "__umgCreateUserWidget: world context UObject pointer is stale");
        }

        void* owning_player_ptr = argc >= 3 ? JSUObject::get_uobject(ctx, argv[2]) : nullptr;
        if (owning_player_ptr && !seh_probe_uobject_safe(owning_player_ptr))
        {
            return JS_ThrowInternalError(ctx, "__umgCreateUserWidget: owning player UObject pointer is stale");
        }

        Unreal::UObject* world_context = world_context_ptr ? static_cast<Unreal::UObject*>(world_context_ptr) : nullptr;
        Unreal::UObject* owning_player = owning_player_ptr ? static_cast<Unreal::UObject*>(owning_player_ptr) : nullptr;

        if (!world_context)
        {
            world_context = owning_player ? owning_player : find_first_object_of(STR("GameInstance"));
            if (!world_context)
            {
                world_context = find_first_object_of(STR("PlayerController"));
            }
            if (!world_context)
            {
                world_context = find_first_object_of(STR("World"));
            }
        }
        if (!owning_player)
        {
            owning_player = find_first_object_of(STR("PlayerController"));
        }

        if (!world_context)
        {
            return JS_ThrowInternalError(ctx, "__umgCreateUserWidget: could not resolve a world context object");
        }

        if (!ensure_umg_dispatcher(mod))
        {
            return JS_ThrowInternalError(ctx, "__umgCreateUserWidget: UMG dispatcher failed to register");
        }

        Unreal::UClass* class_capture = widget_class;
        Unreal::UObject* world_context_capture = world_context;
        Unreal::UObject* owning_player_capture = owning_player;

        void* result = mod->m_umg_dispatcher.dispatch_sync(
            [class_capture, world_context_capture, owning_player_capture]() -> void* {
                if (!seh_probe_uobject_safe(class_capture) ||
                    !seh_probe_uobject_safe(world_context_capture) ||
                    (owning_player_capture && !seh_probe_uobject_safe(owning_player_capture)))
                {
                    return nullptr;
                }

                Unreal::UObject* library_cdo = resolve_widget_blueprint_library_cdo();
                if (!library_cdo)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCreateUserWidget: failed to resolve WidgetBlueprintLibrary CDO\n"));
                    return nullptr;
                }

                Unreal::UFunction* create_function = library_cdo->GetFunctionByNameInChain(STR("Create"));
                if (!create_function)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCreateUserWidget: WidgetBlueprintLibrary.Create not found\n"));
                    return nullptr;
                }

                const int32_t params_size = create_function->GetParmsSize();
                if (params_size <= 0)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCreateUserWidget: invalid params size for WidgetBlueprintLibrary.Create\n"));
                    return nullptr;
                }

                void* params_memory = calloc(1, params_size);
                if (!params_memory)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCreateUserWidget: failed to allocate params memory\n"));
                    return nullptr;
                }

                set_function_object_param_by_name(create_function, params_memory, STR("WorldContextObject"), world_context_capture);
                if (!set_function_object_param_by_name(create_function, params_memory, STR("WidgetType"), class_capture))
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCreateUserWidget: failed to bind WidgetType parameter\n"));
                    free(params_memory);
                    return nullptr;
                }
                set_function_object_param_by_name(create_function, params_memory, STR("OwningPlayer"), owning_player_capture);

                if (!safe_process_event(library_cdo, create_function, params_memory))
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCreateUserWidget: WidgetBlueprintLibrary.Create crashed during ProcessEvent\n"));
                    free(params_memory);
                    return nullptr;
                }

                Unreal::UObject* created_widget = get_function_return_object(create_function, params_memory);
                free(params_memory);
                return created_widget;
            });

        if (!result)
        {
            return JS_ThrowInternalError(ctx, "__umgCreateUserWidget failed");
        }

        return JSUObject::create(ctx, result);
    }

    JSValue js_umg_clone_user_widget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;
        if (argc < 1)
        {
            return JS_ThrowTypeError(ctx, "__umgCloneUserWidget requires: sourceWidget [, outerObject]");
        }

        JSMod* mod = get_js_mod(ctx);
        if (!mod)
        {
            return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
        }

        void* source_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!source_ptr)
        {
            return JS_ThrowTypeError(ctx, "__umgCloneUserWidget: sourceWidget must be a UObject");
        }
        if (!seh_probe_uobject_safe(source_ptr))
        {
            return JS_ThrowInternalError(ctx, "__umgCloneUserWidget: sourceWidget UObject pointer is stale");
        }

        void* outer_ptr = argc >= 2 ? JSUObject::get_uobject(ctx, argv[1]) : nullptr;
        if (outer_ptr && !seh_probe_uobject_safe(outer_ptr))
        {
            return JS_ThrowInternalError(ctx, "__umgCloneUserWidget: outer UObject pointer is stale");
        }

        Unreal::UObject* source_widget = static_cast<Unreal::UObject*>(source_ptr);
        Unreal::UObject* outer = outer_ptr ? static_cast<Unreal::UObject*>(outer_ptr) : nullptr;

        if (!ensure_umg_dispatcher(mod))
        {
            return JS_ThrowInternalError(ctx, "__umgCloneUserWidget: UMG dispatcher failed to register");
        }

        Unreal::UObject* source_capture = source_widget;
        Unreal::UObject* outer_capture = outer;

        void* result = mod->m_umg_dispatcher.dispatch_sync(
            [source_capture, outer_capture]() -> void* {
                if (!seh_probe_uobject_safe(source_capture))
                {
                    return nullptr;
                }

                Unreal::UClass* source_class = source_capture->GetClassPrivate();
                if (!source_class || !seh_probe_uobject_safe(source_class))
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCloneUserWidget: source widget class is unavailable\n"));
                    return nullptr;
                }

                Unreal::UObject* clone_outer = outer_capture;
                if (!clone_outer)
                {
                    clone_outer = source_capture->GetOuterPrivate();
                    if (clone_outer && !seh_probe_uobject_safe(clone_outer))
                    {
                        clone_outer = nullptr;
                    }
                }
                if (!clone_outer)
                {
                    clone_outer = find_first_object_of(STR("GameInstance"));
                }
                if (!clone_outer)
                {
                    clone_outer = find_first_object_of(STR("PlayerController"));
                }
                if (!clone_outer)
                {
                    clone_outer = find_first_object_of(STR("World"));
                }
                if (!clone_outer)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCloneUserWidget: failed to resolve clone outer\n"));
                    return nullptr;
                }

                Unreal::UObject* cloned_widget = nullptr;
                if (!seh_construct_object_from_template(source_class, clone_outer, source_capture, &cloned_widget))
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCloneUserWidget: StaticConstructObject with template crashed\n"));
                    return nullptr;
                }

                if (!cloned_widget)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCloneUserWidget: StaticConstructObject with template returned nullptr\n"));
                    return nullptr;
                }

                return cloned_widget;
            });

        if (!result)
        {
            return JS_ThrowInternalError(ctx, "__umgCloneUserWidget failed");
        }

        return JSUObject::create(ctx, result);
    }

    // ============================================
    // JS: __umgDispatchSync(callback) — blocks until game thread executes
    // Passes the callback's return value back. The callback runs in JS thread
    // but its body is serialized to ops dispatched on game thread.
    //
    // For v1 this is a simplified version that dispatches a single
    // CallFunction-like operation synchronously.
    // ============================================

    JSValue js_umg_dispatch_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "__umgDispatchSync requires: object, functionName [, ...args]");

        JSMod* mod = get_js_mod(ctx);
        if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");

        if (!ensure_umg_dispatcher(mod))
            return JS_ThrowInternalError(ctx, "__umgDispatchSync: UMG dispatcher failed to register");

        void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!obj_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject");
        if (!seh_probe_uobject_safe(obj_ptr))
            return JS_ThrowInternalError(ctx, "__umgDispatchSync: UObject pointer is stale");
        Unreal::UObject* object = static_cast<Unreal::UObject*>(obj_ptr);

        const char* func_name = JS_ToCString(ctx, argv[1]);
        if (!func_name)
            return JS_ThrowTypeError(ctx, "Second argument must be a function name string");
        std::wstring wide_func_name = utf8_to_wide(func_name);
        JS_FreeCString(ctx, func_name);

        try
        {
            Unreal::UFunction* function = object->GetFunctionByNameInChain(wide_func_name.c_str());
            if (!function)
                return JS_ThrowReferenceError(ctx, "Function not found on object");

            int32_t params_size = function->GetParmsSize();
            void* params_memory = nullptr;
            if (params_size > 0)
            {
                params_memory = calloc(1, params_size);
                if (!params_memory)
                    return JS_ThrowInternalError(ctx, "Failed to allocate params memory");
            }

            // shared_ptr ensures params_memory is freed exactly once even if
            // dispatch_sync times out and the lambda is still pending.
            auto params_guard = std::shared_ptr<void>(params_memory, [](void* p) { if (p) free(p); });

            int js_arg_index = 2;
            for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(function, Unreal::EFieldIterationFlags::IncludeDeprecated))
            {
                if (!prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm)) continue;
                if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm)) continue;
                if (js_arg_index >= argc) break;

                void* prop_addr = prop->ContainerPtrToValuePtr<void>(params_memory);
                jsvalue_to_property(ctx, prop, prop_addr, argv[js_arg_index]);
                js_arg_index++;
            }

            Unreal::UObject* obj_capture = object;
            Unreal::UFunction* func_capture = function;

            void* raw_result = mod->m_umg_dispatcher.dispatch_sync(
                [obj_capture, func_capture, params_memory, params_guard]() -> void* {
                    if (!seh_probe_uobject_safe(obj_capture) || !seh_probe_uobject_safe(func_capture))
                        return nullptr;
                    if (!safe_process_event(obj_capture, func_capture, params_memory))
                    {
                        Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgDispatchSync: ProcessEvent failed on game thread\n"));
                        return nullptr;
                    }
                    return make_dispatch_success_token(params_memory);
                });

            if (!raw_result)
                return JS_ThrowInternalError(ctx, "__umgDispatchSync: ProcessEvent failed on game thread");

            JSValue ret = JS_TRUE;
            if (params_memory)
            {
                for (Unreal::FProperty* ret_prop : Unreal::TFieldRange<Unreal::FProperty>(function, Unreal::EFieldIterationFlags::IncludeDeprecated))
                {
                    if (ret_prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
                    {
                        void* ret_addr = ret_prop->ContainerPtrToValuePtr<void>(params_memory);
                        ret = property_to_jsvalue(ctx, ret_prop, ret_addr);
                        break;
                    }
                }
            }

            return ret;
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgDispatchSync exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "__umgDispatchSync failed due to exception");
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgDispatchSync unknown exception\n"));
            return JS_ThrowInternalError(ctx, "__umgDispatchSync failed due to unknown exception");
        }
    }

    // ============================================
    // JS: __umgDispatchAsync(object, functionName, ...args) — fire-and-forget
    // ============================================

    JSValue js_umg_dispatch_async(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "__umgDispatchAsync requires: object, functionName [, ...args]");

        JSMod* mod = get_js_mod(ctx);
        if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");

        if (!ensure_umg_dispatcher(mod))
            return JS_ThrowInternalError(ctx, "__umgDispatchAsync: UMG dispatcher failed to register");

        void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!obj_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject");
        if (!seh_probe_uobject_safe(obj_ptr))
            return JS_ThrowInternalError(ctx, "__umgDispatchAsync: UObject pointer is stale");
        Unreal::UObject* object = static_cast<Unreal::UObject*>(obj_ptr);

        const char* func_name = JS_ToCString(ctx, argv[1]);
        if (!func_name)
            return JS_ThrowTypeError(ctx, "Second argument must be a function name string");
        std::wstring wide_func_name = utf8_to_wide(func_name);
        JS_FreeCString(ctx, func_name);

        void* params_memory = nullptr;
        try
        {
            Unreal::UFunction* function = object->GetFunctionByNameInChain(wide_func_name.c_str());
            if (!function)
                return JS_ThrowReferenceError(ctx, "Function not found on object");

            int32_t params_size = function->GetParmsSize();
            if (params_size > 0)
            {
                params_memory = calloc(1, params_size);
                if (!params_memory)
                    return JS_ThrowInternalError(ctx, "Failed to allocate params memory");
            }

            int js_arg_index = 2;
            for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(function, Unreal::EFieldIterationFlags::IncludeDeprecated))
            {
                if (!prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm)) continue;
                if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm)) continue;
                if (js_arg_index >= argc) break;

                void* prop_addr = prop->ContainerPtrToValuePtr<void>(params_memory);
                jsvalue_to_property(ctx, prop, prop_addr, argv[js_arg_index]);
                js_arg_index++;
            }

            Unreal::UObject* obj_capture = object;
            Unreal::UFunction* func_capture = function;

            mod->m_umg_dispatcher.dispatch_async([obj_capture, func_capture, params_memory]() {
                if (!seh_probe_uobject_safe(obj_capture) || !seh_probe_uobject_safe(func_capture))
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgDispatchAsync: stale UObject/UFunction at execution time, skipping\n"));
                    if (params_memory) free(params_memory);
                    return;
                }
                if (!safe_process_event(obj_capture, func_capture, params_memory))
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgDispatchAsync: ProcessEvent failed on game thread\n"));
                }
                if (params_memory) free(params_memory);
            });
            params_memory = nullptr;

            return JS_UNDEFINED;
        }
        catch (const std::exception& e)
        {
            if (params_memory) free(params_memory);
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgDispatchAsync exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "__umgDispatchAsync failed due to exception");
        }
        catch (...)
        {
            if (params_memory) free(params_memory);
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgDispatchAsync unknown exception\n"));
            return JS_ThrowInternalError(ctx, "__umgDispatchAsync failed due to unknown exception");
        }
    }

    // ============================================
    // JS: __umgSetUserWidgetRoot(userWidget, rootWidget) — set root via WidgetTree (no SetRootWidget UFunction)
    // ============================================

    JSValue js_umg_set_user_widget_root(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "__umgSetUserWidgetRoot requires: userWidget, rootWidget");

        JSMod* mod = get_js_mod(ctx);
        if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");

        void* user_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!user_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject (UserWidget)");
        if (!seh_probe_uobject_safe(user_ptr))
            return JS_ThrowInternalError(ctx, "__umgSetUserWidgetRoot: UserWidget pointer is stale");
        void* root_ptr = JSUObject::get_uobject(ctx, argv[1]);
        if (!root_ptr)
            return JS_ThrowTypeError(ctx, "Second argument must be a UObject (root widget)");
        if (!seh_probe_uobject_safe(root_ptr))
            return JS_ThrowInternalError(ctx, "__umgSetUserWidgetRoot: root widget pointer is stale");

        Unreal::UObject* user_widget = static_cast<Unreal::UObject*>(user_ptr);
        Unreal::UObject* root_widget = static_cast<Unreal::UObject*>(root_ptr);

        Unreal::UObject* user_capture = user_widget;
        Unreal::UObject* root_capture = root_widget;

        void* raw_result = mod->m_umg_dispatcher.dispatch_sync([user_capture, root_capture]() -> void* {
            void* tree_storage = user_capture->GetValuePtrByPropertyNameInChain(STR("WidgetTree"));
            if (!tree_storage)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgSetUserWidgetRoot: WidgetTree property not found on UserWidget\n"));
                return reinterpret_cast<void*>(static_cast<uintptr_t>(1));
            }
            Unreal::UObject* widget_tree = *static_cast<Unreal::UObject**>(tree_storage);
            if (!widget_tree)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] __umgSetUserWidgetRoot: WidgetTree is null, creating one...\n"));
                Unreal::UClass* wt_class = static_cast<Unreal::UClass*>(
                    Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, STR("/Script/UMG.WidgetTree")));
                if (!wt_class)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgSetUserWidgetRoot: WidgetTree class not found\n"));
                    return reinterpret_cast<void*>(static_cast<uintptr_t>(2));
                }
                Unreal::FStaticConstructObjectParameters params(wt_class);
                params.Outer = user_capture;
                widget_tree = Unreal::UObjectGlobals::StaticConstructObject(params);
                if (!widget_tree)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgSetUserWidgetRoot: Failed to create WidgetTree\n"));
                    return reinterpret_cast<void*>(static_cast<uintptr_t>(3));
                }
                *static_cast<Unreal::UObject**>(tree_storage) = widget_tree;
                Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] __umgSetUserWidgetRoot: WidgetTree created and assigned\n"));
            }
            void* root_storage = widget_tree->GetValuePtrByPropertyNameInChain(STR("RootWidget"));
            if (!root_storage)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgSetUserWidgetRoot: RootWidget property not found on WidgetTree\n"));
                return reinterpret_cast<void*>(static_cast<uintptr_t>(4));
            }
            *static_cast<Unreal::UObject**>(root_storage) = root_capture;
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] __umgSetUserWidgetRoot: RootWidget set successfully\n"));
            return nullptr;
        });

        if (raw_result != nullptr)
        {
            uintptr_t err = reinterpret_cast<uintptr_t>(raw_result);
            return JS_ThrowInternalError(ctx, "__umgSetUserWidgetRoot failed (code %d)", (int)err);
        }
        return JS_UNDEFINED;
    }

    // ============================================
    // JS: __umgConstructWidget(userWidget, widgetClassName) — create widget via WidgetTree.ConstructWidget
    // ============================================

    JSValue js_umg_construct_widget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "__umgConstructWidget requires: userWidget, widgetClassName");

        JSMod* mod = get_js_mod(ctx);
        if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");

        void* user_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!user_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject (UserWidget)");
        if (!seh_probe_uobject_safe(user_ptr))
            return JS_ThrowInternalError(ctx, "__umgConstructWidget: UserWidget pointer is stale");

        const char* class_name = JS_ToCString(ctx, argv[1]);
        if (!class_name)
            return JS_ThrowTypeError(ctx, "Second argument must be a widget class name string");
        std::string class_path = std::string("/Script/UMG.") + class_name;
        JS_FreeCString(ctx, class_name);

        std::wstring wide_path = utf8_to_wide(class_path);
        Unreal::UObject* user_widget = static_cast<Unreal::UObject*>(user_ptr);

        Unreal::UObject* user_capture = user_widget;
        std::wstring path_capture = wide_path;

        void* result = mod->m_umg_dispatcher.dispatch_sync([user_capture, path_capture]() -> void* {
            void* tree_storage = user_capture->GetValuePtrByPropertyNameInChain(STR("WidgetTree"));
            if (!tree_storage)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgConstructWidget: WidgetTree property not found\n"));
                return nullptr;
            }
            Unreal::UObject* widget_tree = *static_cast<Unreal::UObject**>(tree_storage);
            if (!widget_tree)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] __umgConstructWidget: WidgetTree is null, creating one...\n"));
                Unreal::UClass* wt_class = static_cast<Unreal::UClass*>(
                    Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, STR("/Script/UMG.WidgetTree")));
                if (!wt_class)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgConstructWidget: WidgetTree class not found\n"));
                    return nullptr;
                }
                Unreal::FStaticConstructObjectParameters wt_params(wt_class);
                wt_params.Outer = user_capture;
                widget_tree = Unreal::UObjectGlobals::StaticConstructObject(wt_params);
                if (!widget_tree)
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgConstructWidget: Failed to create WidgetTree\n"));
                    return nullptr;
                }
                *static_cast<Unreal::UObject**>(tree_storage) = widget_tree;
                Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] __umgConstructWidget: WidgetTree created and assigned\n"));
            }

            Unreal::UClass* widget_class = static_cast<Unreal::UClass*>(
                Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, path_capture.c_str()));
            if (!widget_class)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgConstructWidget: class not found: {}\n"), path_capture);
                return nullptr;
            }

            Unreal::FStaticConstructObjectParameters params(widget_class, widget_tree);
            params.SetFlags = Unreal::EObjectFlags::RF_Transient;
            Unreal::UObject* new_widget = Unreal::UObjectGlobals::StaticConstructObject(params);
            if (!new_widget)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgConstructWidget: StaticConstructObject failed\n"));
                return nullptr;
            }

            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] __umgConstructWidget: created widget of class {}\n"), path_capture);
            return static_cast<void*>(new_widget);
        });

        if (!result)
            return JS_ThrowInternalError(ctx, "__umgConstructWidget failed");

        return JSUObject::create(ctx, result);
    }

} // namespace RC::JSScript
