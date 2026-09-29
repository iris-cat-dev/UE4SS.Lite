#include "JSInternal.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObjectArray.hpp>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace RC::JSScript
{
    // Delegate callback tracking

    struct DelegateCallbackEntry
    {
        int id{0};
        JSMod* mod{nullptr};
        JSContext* ctx{nullptr};
        JSValue callback{JS_UNDEFINED};
        Unreal::UObject* owner{nullptr};
        std::wstring delegate_name{};
        Unreal::FName sig_func_name{};
        int64_t shared_hook_id{0};
    };

    struct SharedSignatureHookState
    {
        int64_t id{0};
        JSMod* mod{nullptr};
        JSContext* ctx{nullptr};
        Unreal::UFunction* sig_func{nullptr};
        Unreal::FName sig_func_name{};
        Unreal::UnrealScriptFunction original_func_ptr{nullptr};
        uint32_t original_function_flags{0};
        bool original_was_native{false};
        int32_t pre_hook_id{0};
        int32_t post_hook_id{0};
        int32_t ref_count{0};
        JSValue dispatch_callback{JS_UNDEFINED};
    };

    static std::vector<DelegateCallbackEntry> s_delegate_callbacks;
    static std::unordered_map<int64_t, SharedSignatureHookState> s_shared_signature_hooks;
    static std::unordered_map<Unreal::UFunction*, int64_t> s_shared_signature_hook_ids;
    static int s_next_delegate_callback_id = 0;
    static int64_t s_next_shared_signature_hook_id = 1;

    // Cache of (UClass*, FName-bits) pairs already injected into the class FuncMap.
    // Repeated mounts of the same widget class would otherwise rewrite the same
    // FuncMap entry on every BindDelegateCallback call; the entry is permanent for
    // the lifetime of the UClass so a hit-through is safe and a SEH path is avoided.
    struct FuncMapInjectionKey
    {
        Unreal::UClass* cls;
        uint64_t fname_bits;

        bool operator==(const FuncMapInjectionKey& other) const noexcept
        {
            return cls == other.cls && fname_bits == other.fname_bits;
        }
    };

    struct FuncMapInjectionKeyHash
    {
        size_t operator()(const FuncMapInjectionKey& key) const noexcept
        {
            const size_t cls_hash = std::hash<void*>{}(static_cast<void*>(key.cls));
            const size_t name_hash = std::hash<uint64_t>{}(key.fname_bits);
            return cls_hash ^ (name_hash + 0x9E3779B97F4A7C15ULL + (cls_hash << 6) + (cls_hash >> 2));
        }
    };

    static std::unordered_set<FuncMapInjectionKey, FuncMapInjectionKeyHash> s_funcmap_injection_cache;
    static std::mutex s_funcmap_injection_cache_mutex;

    static bool seh_restore_function_state(
        Unreal::UFunction* func,
        Unreal::UnrealScriptFunction original_func_ptr,
        uint32_t original_function_flags);

    static void destroy_shared_signature_hook(SharedSignatureHookState& state)
    {
        if (state.mod && (state.pre_hook_id > 0 || state.post_hook_id > 0))
        {
            (void)state.mod->unregister_ufunction_hook(state.pre_hook_id, state.post_hook_id);
        }

        state.pre_hook_id = 0;
        state.post_hook_id = 0;

        if (state.sig_func)
        {
            (void)seh_restore_function_state(
                state.sig_func,
                state.original_func_ptr,
                state.original_function_flags);
        }

        if (state.ctx && !JS_IsUndefined(state.dispatch_callback))
        {
            JS_FreeValue(state.ctx, state.dispatch_callback);
            state.dispatch_callback = JS_UNDEFINED;
        }

        state.ref_count = 0;
    }

    void reset_delegate_statics()
    {
        for (auto& entry : s_delegate_callbacks)
        {
            if (entry.ctx && !JS_IsUndefined(entry.callback))
            {
                JS_FreeValue(entry.ctx, entry.callback);
            }
        }
        s_delegate_callbacks.clear();

        for (auto& [_, state] : s_shared_signature_hooks)
        {
            destroy_shared_signature_hook(state);
        }
        s_shared_signature_hooks.clear();
        s_shared_signature_hook_ids.clear();
        s_next_delegate_callback_id = 0;
        s_next_shared_signature_hook_id = 1;

        {
            std::lock_guard<std::mutex> lock(s_funcmap_injection_cache_mutex);
            s_funcmap_injection_cache.clear();
        }
    }

    // SEH-isolated delegate property lookup

    struct DelegateLookupResult
    {
        Unreal::FMulticastDelegateProperty* prop = nullptr;
        void* prop_value = nullptr;
    };

    static DelegateLookupResult seh_find_delegate_property(
        Unreal::UObject* owner, const wchar_t* delegate_name)
    {
        DelegateLookupResult result{};
        __try
        {
            auto* p = owner->GetPropertyByNameInChain(
                Unreal::FName(delegate_name, Unreal::FNAME_Find));
            if (!p) return result;

            if (!p->IsA<Unreal::FMulticastDelegateProperty>() &&
                !p->IsA<Unreal::FMulticastInlineDelegateProperty>())
                return result;

            result.prop = static_cast<Unreal::FMulticastDelegateProperty*>(p);
            result.prop_value = p->ContainerPtrToValuePtr<void>(owner);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result.prop = nullptr;
            result.prop_value = nullptr;
        }
        return result;
    }

    static Unreal::UFunction* seh_get_signature_function(Unreal::FMulticastDelegateProperty* prop)
    {
        __try
        {
            return prop->GetSignatureFunction();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static uint64_t fname_to_bits(const Unreal::FName& name)
    {
        uint64_t bits = 0;
        constexpr size_t copy_size = sizeof(Unreal::FName) < sizeof(uint64_t)
            ? sizeof(Unreal::FName)
            : sizeof(uint64_t);
        std::memcpy(&bits, &name, copy_size);
        return bits;
    }

    // SEH-protected probe to verify a UObject pointer is still alive in the
    // global UObject array. Mirrors the same probe used in JSHook.cpp /
    // JSGameThreadDispatcher.cpp; kept local here to avoid widening the
    // shared header surface.
    static int seh_probe_uobject_alive(void* ptr)
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

    static Unreal::UClass* seh_get_owner_class(Unreal::UObject* owner)
    {
        if (!owner)
        {
            return nullptr;
        }

        __try
        {
            return owner->GetClassPrivate();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool seh_funcmap_add(
        Unreal::UClass* owner_class, Unreal::FName func_name, Unreal::UFunction* func)
    {
        __try
        {
            auto& func_map = owner_class->GetFuncMap();
            func_map.Add(func_name, Unreal::TObjectPtr<Unreal::UFunction>(func));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool seh_inject_into_funcmap(
        Unreal::UObject* owner, Unreal::FName func_name, Unreal::UFunction* func)
    {
        Unreal::UClass* owner_class = seh_get_owner_class(owner);
        if (!owner_class)
        {
            return false;
        }

        FuncMapInjectionKey key{owner_class, fname_to_bits(func_name)};
        {
            std::lock_guard<std::mutex> lock(s_funcmap_injection_cache_mutex);
            if (s_funcmap_injection_cache.find(key) != s_funcmap_injection_cache.end())
            {
                return true;
            }
        }

        if (!seh_funcmap_add(owner_class, func_name, func))
        {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(s_funcmap_injection_cache_mutex);
            s_funcmap_injection_cache.insert(key);
        }
        return true;
    }

    // No-op native stub for DelegateFunctions.
    // When a delegate broadcasts, ProcessEvent sees FUNC_Native and calls Invoke,
    // which goes through the hook trampoline and fires our JS callback.
    static void delegate_callback_native_stub(
        Unreal::UObject* /*Context*/, Unreal::FFrame& /*Stack*/, void* /*RESULT_DECL*/)
    {
    }

    // Convert a script-type DelegateFunction into a "native" function so that
    // RegisterHook uses the native hook path (RegisterPreHook/PostHook)
    // instead of the GlobalScriptHooks path (ProcessLocalScriptFunction).
    // The latter never fires for DelegateFunctions because they have empty bytecode.
    static bool seh_make_func_native(Unreal::UFunction* func)
    {
        __try
        {
            if (func->HasAnyFunctionFlags(Unreal::EFunctionFlags::FUNC_Native))
                return true;

            func->GetFunctionFlags() |=
                static_cast<uint32_t>(Unreal::EFunctionFlags::FUNC_Native);
            func->SetFuncPtr(delegate_callback_native_stub);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool seh_capture_function_state(
        Unreal::UFunction* func,
        Unreal::UnrealScriptFunction& out_func_ptr,
        uint32_t& out_function_flags,
        bool& out_is_native)
    {
        out_func_ptr = nullptr;
        out_function_flags = 0;
        out_is_native = false;

        __try
        {
            out_func_ptr = func->GetFuncPtr();
            out_function_flags = func->GetFunctionFlags();
            out_is_native = func->HasAnyFunctionFlags(Unreal::EFunctionFlags::FUNC_Native);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool seh_restore_function_state(
        Unreal::UFunction* func,
        Unreal::UnrealScriptFunction original_func_ptr,
        uint32_t original_function_flags)
    {
        if (!func)
        {
            return false;
        }

        __try
        {
            func->SetFuncPtr(original_func_ptr);
            func->GetFunctionFlags() = original_function_flags;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // DRG (and possibly other shipping builds) never populate
    // FUObjectItem::SerialNumber - it stays 0 for every object.
    // UE4SS's FWeakObjectPtr::operator= reads that 0, making the weak
    // pointer permanently null.  Both the game's vtable AddDelegate and
    // UE4SS's Get() then treat the entry as unbound.
    //
    // Fix: if serial == 0 after BindUFunction, write a synthetic non-zero
    // serial into BOTH the FUObjectItem (so future lookups also work) and
    // the FScriptDelegate's FWeakObjectPtr.  We use 1 as the sentinel.
    static constexpr int32_t SYNTH_SERIAL = 1;

    static void seh_fix_weak_object_ptr(Unreal::FScriptDelegate& sd, Unreal::UObject* owner)
    {
        __try
        {
            auto& wop = sd.GetUObjectRef();
            int32_t idx = wop.ObjectIndex;
            int32_t ser = wop.ObjectSerialNumber;

            if (ser != 0)
                return;
            if (idx <= 0)
                return;

            auto* item = Unreal::FUObjectArray::IndexToObject(idx);
            if (!item)
                return;

            if (item->GetUObject() != owner)
            {
                Output::send<LogLevel::Error>(
                    STR("[UE4SSL.JavaScript] fix_weak_ptr: FUObjectItem UObject mismatch, skipping\n"));
                return;
            }

            int32_t existing = item->GetSerialNumber();
            if (existing == 0)
            {
                item->GetSerialNumber() = SYNTH_SERIAL;
                Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.JavaScript] fix_weak_ptr: wrote synthetic serial {} into FUObjectItem[{}]\n"),
                    SYNTH_SERIAL, idx);
            }
            else
            {
                Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.JavaScript] fix_weak_ptr: FUObjectItem serial already {} (idx={})\n"),
                    existing, idx);
            }

            wop.ObjectSerialNumber = item->GetSerialNumber();
            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] fix_weak_ptr: sd.ObjectSerialNumber={} ObjectIndex={}\n"),
                wop.ObjectSerialNumber, idx);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Output::send<LogLevel::Error>(
                STR("[UE4SSL.JavaScript] fix_weak_ptr: SEH exception\n"));
        }
    }

    static bool seh_ensure_delegate_entry(
        Unreal::FMulticastDelegateProperty* prop, void* prop_value,
        Unreal::UObject* owner, Unreal::FName sig_func_name,
        Unreal::FScriptDelegate& sd)
    {
        __try
        {
            auto* mcd = const_cast<Unreal::FMulticastScriptDelegate*>(
                prop->GetMulticastDelegate(prop_value));
            if (!mcd) return false;

            int total = mcd->InvocationList.Num();
            for (int i = 0; i < total; i++)
            {
                auto& entry = mcd->InvocationList[i];
                if (entry.GetUObject() == owner && entry.GetFunctionName() == sig_func_name)
                {
                    Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.JavaScript] ensure_delegate: entry already present (idx={})\n"), i);
                    return true;
                }
            }

            Output::send<LogLevel::Warning>(
                STR("[UE4SSL.JavaScript] ensure_delegate: entry NOT found (total={}), adding via fallback\n"),
                total);

            mcd->InvocationList.Add(sd);

            int after = mcd->InvocationList.Num();
            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] ensure_delegate: after Add total={}\n"), after);

            return after > total;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Output::send<LogLevel::Error>(
                STR("[UE4SSL.JavaScript] ensure_delegate: SEH exception\n"));
            return false;
        }
    }

    // BindDelegate(owner, name, target, funcName) -> bool

    JSValue js_bind_delegate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 4)
            return JS_ThrowTypeError(ctx,
                "BindDelegate requires 4 arguments: delegateOwner, delegateName, targetObject, functionName");

        void* owner_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!owner_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject (delegate owner)");
        auto* owner = static_cast<Unreal::UObject*>(owner_ptr);

        const char* delegate_name_c = JS_ToCString(ctx, argv[1]);
        if (!delegate_name_c)
            return JS_ThrowTypeError(ctx, "Second argument must be a string (delegate name)");

        void* target_ptr = JSUObject::get_uobject(ctx, argv[2]);
        if (!target_ptr) {
            JS_FreeCString(ctx, delegate_name_c);
            return JS_ThrowTypeError(ctx, "Third argument must be a UObject (target object)");
        }
        auto* target = static_cast<Unreal::UObject*>(target_ptr);

        const char* func_name_c = JS_ToCString(ctx, argv[3]);
        if (!func_name_c) {
            JS_FreeCString(ctx, delegate_name_c);
            return JS_ThrowTypeError(ctx, "Fourth argument must be a string (function name)");
        }

        std::wstring wide_delegate = utf8_to_wide(std::string(delegate_name_c));
        std::wstring wide_func = utf8_to_wide(std::string(func_name_c));
        JS_FreeCString(ctx, delegate_name_c);
        JS_FreeCString(ctx, func_name_c);

        try
        {
            auto lookup = seh_find_delegate_property(owner, wide_delegate.c_str());
            if (!lookup.prop || !lookup.prop_value)
            {
                Output::send<LogLevel::Error>(
                    STR("[UE4SSL.JavaScript] BindDelegate: '{}' not found or not a multicast delegate\n"),
                    wide_delegate);
                return JS_NewBool(ctx, false);
            }

            Unreal::FScriptDelegate script_delegate;
            Unreal::FName fn(wide_func);
            script_delegate.BindUFunction(target, fn);
            seh_fix_weak_object_ptr(script_delegate, target);
            lookup.prop->AddDelegate(script_delegate, owner, lookup.prop_value);
            seh_ensure_delegate_entry(lookup.prop, lookup.prop_value, target, fn, script_delegate);

            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] BindDelegate: bound '{}' -> target func '{}'\n"),
                wide_delegate, wide_func);

            return JS_NewBool(ctx, true);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(
                STR("[UE4SSL.JavaScript] BindDelegate exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NewBool(ctx, false);
        }
        catch (...)
        {
            return JS_NewBool(ctx, false);
        }
    }

    // UnbindDelegate(owner, name, target, funcName) -> bool

    JSValue js_unbind_delegate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 4)
            return JS_ThrowTypeError(ctx,
                "UnbindDelegate requires 4 arguments: delegateOwner, delegateName, targetObject, functionName");

        void* owner_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!owner_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject (delegate owner)");
        auto* owner = static_cast<Unreal::UObject*>(owner_ptr);

        const char* delegate_name_c = JS_ToCString(ctx, argv[1]);
        if (!delegate_name_c)
            return JS_ThrowTypeError(ctx, "Second argument must be a string (delegate name)");

        void* target_ptr = JSUObject::get_uobject(ctx, argv[2]);
        if (!target_ptr) {
            JS_FreeCString(ctx, delegate_name_c);
            return JS_ThrowTypeError(ctx, "Third argument must be a UObject (target object)");
        }
        auto* target = static_cast<Unreal::UObject*>(target_ptr);

        const char* func_name_c = JS_ToCString(ctx, argv[3]);
        if (!func_name_c) {
            JS_FreeCString(ctx, delegate_name_c);
            return JS_ThrowTypeError(ctx, "Fourth argument must be a string (function name)");
        }

        std::wstring wide_delegate = utf8_to_wide(std::string(delegate_name_c));
        std::wstring wide_func = utf8_to_wide(std::string(func_name_c));
        JS_FreeCString(ctx, delegate_name_c);
        JS_FreeCString(ctx, func_name_c);

        try
        {
            auto lookup = seh_find_delegate_property(owner, wide_delegate.c_str());
            if (!lookup.prop || !lookup.prop_value)
                return JS_NewBool(ctx, false);

            Unreal::FScriptDelegate script_delegate;
            script_delegate.BindUFunction(target, Unreal::FName(wide_func));
            lookup.prop->RemoveDelegate(script_delegate, owner, lookup.prop_value);

            return JS_NewBool(ctx, true);
        }
        catch (...)
        {
            return JS_NewBool(ctx, false);
        }
    }

    // ClearDelegate(owner, name) -> bool

    JSValue js_clear_delegate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "ClearDelegate requires 2 arguments: delegateOwner, delegateName");

        void* owner_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!owner_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject");
        auto* owner = static_cast<Unreal::UObject*>(owner_ptr);

        const char* name_c = JS_ToCString(ctx, argv[1]);
        if (!name_c)
            return JS_ThrowTypeError(ctx, "Second argument must be a string");

        std::wstring wide_name = utf8_to_wide(std::string(name_c));
        JS_FreeCString(ctx, name_c);

        try
        {
            auto lookup = seh_find_delegate_property(owner, wide_name.c_str());
            if (!lookup.prop || !lookup.prop_value)
                return JS_NewBool(ctx, false);

            lookup.prop->ClearDelegate(owner, lookup.prop_value);
            return JS_NewBool(ctx, true);
        }
        catch (...)
        {
            return JS_NewBool(ctx, false);
        }
    }

    static int seh_count_delegate_entries(
        Unreal::FMulticastDelegateProperty* prop, void* prop_value,
        Unreal::UObject* expected_owner, Unreal::FName expected_func_name)
    {
        __try
        {
            auto* delegate = prop->GetMulticastDelegate(prop_value);
            if (!delegate) return -1;
            int total = delegate->InvocationList.Num();
            int match = 0;
            for (int i = 0; i < total; i++)
            {
                auto& entry = delegate->InvocationList[i];
                auto* obj = entry.GetUObject();
                auto fname = entry.GetFunctionName();
                if (obj == expected_owner && fname == expected_func_name) match++;
            }
            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] BindDelegateCallback: InvocationList total={} ourEntry={}\n"),
                total, match);
            return total;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Output::send<LogLevel::Warning>(
                STR("[UE4SSL.JavaScript] BindDelegateCallback: InvocationList read SEH exception\n"));
            return -2;
        }
    }

    static Unreal::UFunction* seh_find_in_funcmap(Unreal::UObject* owner, Unreal::FName name)
    {
        __try
        {
            auto* cls = owner->GetClassPrivate();
            if (!cls) return nullptr;
            auto& fmap = cls->GetFuncMap();
            auto* found = fmap.Find(name);
            return found ? found->Get() : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    struct SehFuncPtrResult { uintptr_t fptr; bool is_native; bool ok; };

    static SehFuncPtrResult seh_read_func_ptr(Unreal::UFunction* func)
    {
        SehFuncPtrResult r{0, false, false};
        __try
        {
            r.fptr = std::bit_cast<uintptr_t>(func->GetFuncPtr());
            r.is_native = func->HasAnyFunctionFlags(Unreal::EFunctionFlags::FUNC_Native);
            r.ok = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return r;
    }

    static SharedSignatureHookState* find_shared_signature_hook_by_id(int64_t shared_hook_id)
    {
        auto it = s_shared_signature_hooks.find(shared_hook_id);
        return it != s_shared_signature_hooks.end() ? &it->second : nullptr;
    }

    static SharedSignatureHookState* find_shared_signature_hook_by_function(Unreal::UFunction* sig_func)
    {
        auto id_it = s_shared_signature_hook_ids.find(sig_func);
        if (id_it == s_shared_signature_hook_ids.end())
        {
            return nullptr;
        }

        return find_shared_signature_hook_by_id(id_it->second);
    }

    struct DelegateDispatchTarget
    {
        JSMod* mod{nullptr};
        JSValue callback{JS_UNDEFINED};
    };

    static JSValue delegate_shared_dispatch(
        JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValueConst* func_data)
    {
        (void)this_val;
        (void)magic;

        int64_t shared_hook_id = 0;
        if (JS_ToInt64(ctx, &shared_hook_id, func_data[0]))
        {
            return JS_UNDEFINED;
        }

        auto* state = find_shared_signature_hook_by_id(shared_hook_id);
        if (!state || !state->mod)
        {
            return JS_UNDEFINED;
        }
        if (state->mod->is_safe_mode() ||
            state->mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Delegate))
        {
            return JS_UNDEFINED;
        }

        void* owner_ptr = argc > 0 ? JSUObject::get_uobject(ctx, argv[0]) : nullptr;
        auto* owner = static_cast<Unreal::UObject*>(owner_ptr);
        if (!owner)
        {
            return JS_UNDEFINED;
        }

        std::vector<DelegateDispatchTarget> targets;
        targets.reserve(s_delegate_callbacks.size());
        for (auto& entry : s_delegate_callbacks)
        {
            if (entry.shared_hook_id != shared_hook_id ||
                entry.owner != owner ||
                entry.ctx != ctx ||
                JS_IsUndefined(entry.callback))
            {
                continue;
            }

            targets.push_back({
                entry.mod,
                JS_DupValue(ctx, entry.callback),
            });
        }

        if (targets.empty())
        {
            return JS_UNDEFINED;
        }

        auto detail = state->sig_func_name.ToString();
        for (auto& target : targets)
        {
            JSValue result = JS_UNDEFINED;
            if (!safe_js_call(ctx, target.callback, JS_UNDEFINED, argc, argv, &result))
            {
                if (target.mod)
                {
                    target.mod->report_subsystem_failure(
                        JSMod::GuardedSubsystem::Delegate, L"DelegateDispatch", L"SEH exception");
                }
            }
            else if (JS_IsException(result))
            {
                if (target.mod)
                {
                    target.mod->log_exception(ctx, L"DelegateCallback", detail);
                    target.mod->report_subsystem_failure(
                        JSMod::GuardedSubsystem::Delegate, L"DelegateDispatch", L"JS exception");
                }
            }
            else if (target.mod)
            {
                target.mod->report_subsystem_success(JSMod::GuardedSubsystem::Delegate);
            }

            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, target.callback);
        }

        return JS_UNDEFINED;
    }

    static SharedSignatureHookState* acquire_shared_signature_hook(
        JSMod* mod, JSContext* ctx, Unreal::UFunction* sig_func)
    {
        if (!mod || !ctx || !sig_func)
        {
            return nullptr;
        }

        auto* existing = find_shared_signature_hook_by_function(sig_func);
        if (existing)
        {
            if (existing->ctx != ctx)
            {
                Output::send<LogLevel::Error>(
                    STR("[UE4SSL.JavaScript] BindDelegateCallback: shared signature hook ctx mismatch for {}\n"),
                    sig_func->GetFullName());
                return nullptr;
            }

            if (existing->pre_hook_id <= 0 || existing->post_hook_id <= 0)
            {
                if (!seh_make_func_native(sig_func))
                {
                    Output::send<LogLevel::Error>(
                        STR("[UE4SSL.JavaScript] BindDelegateCallback: failed to reactivate native sigFunc for {}\n"),
                        sig_func->GetFullName());
                    return nullptr;
                }

                try
                {
                    auto [pre_id, post_id] = mod->register_ufunction_hook(
                        ctx, sig_func, JS_UNDEFINED, existing->dispatch_callback);
                    existing->pre_hook_id = pre_id;
                    existing->post_hook_id = post_id;
                }
                catch (...)
                {
                    existing->pre_hook_id = 0;
                    existing->post_hook_id = 0;
                    throw;
                }

                if (existing->pre_hook_id <= 0 || existing->post_hook_id <= 0)
                {
                    Output::send<LogLevel::Error>(
                        STR("[UE4SSL.JavaScript] BindDelegateCallback: failed to reactivate shared signature hook {} for {}\n"),
                        existing->id, sig_func->GetFullName());
                    return nullptr;
                }
            }

            existing->ref_count += 1;
            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] BindDelegateCallback: reusing shared signature hook {} for {}\n"),
                existing->id, sig_func->GetFullName());
            return existing;
        }

        SharedSignatureHookState state{};
        state.id = s_next_shared_signature_hook_id++;
        state.mod = mod;
        state.ctx = ctx;
        state.sig_func = sig_func;
        state.sig_func_name = sig_func->GetNamePrivate();

        if (!seh_capture_function_state(
                sig_func, state.original_func_ptr, state.original_function_flags, state.original_was_native))
        {
            Output::send<LogLevel::Error>(
                STR("[UE4SSL.JavaScript] BindDelegateCallback: failed to capture original signature state for {}\n"),
                sig_func->GetFullName());
            return nullptr;
        }

        if (!seh_make_func_native(sig_func))
        {
            Output::send<LogLevel::Error>(
                STR("[UE4SSL.JavaScript] BindDelegateCallback: failed to make sigFunc native\n"));
            return nullptr;
        }

        JSValue shared_hook_id_value = JS_NewInt64(ctx, state.id);
        if (JS_IsException(shared_hook_id_value))
        {
            (void)seh_restore_function_state(
                sig_func, state.original_func_ptr, state.original_function_flags);
            return nullptr;
        }

        JSValue hook_data_values[1] = {shared_hook_id_value};
        state.dispatch_callback = JS_NewCFunctionData(
            ctx, delegate_shared_dispatch, 3, 0, 1, hook_data_values);
        JS_FreeValue(ctx, shared_hook_id_value);

        if (JS_IsException(state.dispatch_callback))
        {
            state.dispatch_callback = JS_UNDEFINED;
            (void)seh_restore_function_state(
                sig_func, state.original_func_ptr, state.original_function_flags);
            return nullptr;
        }

        try
        {
            auto [pre_id, post_id] = mod->register_ufunction_hook(
                ctx, sig_func, JS_UNDEFINED, state.dispatch_callback);
            state.pre_hook_id = pre_id;
            state.post_hook_id = post_id;
        }
        catch (...)
        {
            if (!JS_IsUndefined(state.dispatch_callback))
            {
                JS_FreeValue(ctx, state.dispatch_callback);
                state.dispatch_callback = JS_UNDEFINED;
            }
            (void)seh_restore_function_state(
                sig_func, state.original_func_ptr, state.original_function_flags);
            throw;
        }

        if (state.pre_hook_id <= 0 || state.post_hook_id <= 0)
        {
            if (!JS_IsUndefined(state.dispatch_callback))
            {
                JS_FreeValue(ctx, state.dispatch_callback);
                state.dispatch_callback = JS_UNDEFINED;
            }
            (void)seh_restore_function_state(
                sig_func, state.original_func_ptr, state.original_function_flags);
            return nullptr;
        }

        state.ref_count = 1;
        auto [it, inserted] = s_shared_signature_hooks.emplace(state.id, std::move(state));
        (void)inserted;
        s_shared_signature_hook_ids[sig_func] = it->first;

        Output::send<LogLevel::Normal>(
            STR("[UE4SSL.JavaScript] BindDelegateCallback: created shared signature hook {} for {}\n"),
            it->second.id, sig_func->GetFullName());
        return &it->second;
    }

    static void release_shared_signature_hook(int64_t shared_hook_id)
    {
        auto it = s_shared_signature_hooks.find(shared_hook_id);
        if (it == s_shared_signature_hooks.end())
        {
            return;
        }

        auto& state = it->second;
        if (state.ref_count > 0)
        {
            state.ref_count -= 1;
        }

        if (state.ref_count > 0)
        {
            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] UnbindDelegateCallback: shared signature hook {} ref_count={}\n"),
                state.id, state.ref_count);
            return;
        }
        Output::send<LogLevel::Normal>(
            STR("[UE4SSL.JavaScript] UnbindDelegateCallback: shared signature hook {} is idle; unregistering detour and leaving native stub\n"),
            state.id);

        if (state.mod && (state.pre_hook_id > 0 || state.post_hook_id > 0))
        {
            (void)state.mod->unregister_ufunction_hook(state.pre_hook_id, state.post_hook_id);
        }
        state.pre_hook_id = 0;
        state.post_hook_id = 0;
    }

    // BindDelegateCallback(owner, name, callback) -> int (callbackId)
    //
    // Binds a JS callback directly to a multicast delegate, eliminating the need
    // for any Blueprint UFunction intermediary.
    //
    // Mechanism:
    //   1. Get the delegate's signature function (e.g. NewMessageSig__DelegateSignature).
    //   2. Inject it into the owner class's FuncMap so that
    //      owner->FindFunction(sigFuncName) succeeds at broadcast time.
    //   3. Bind the delegate to (owner, sigFuncName).
    //   4. Register a post-hook on the signature function using the existing hook system.

    JSValue js_bind_delegate_callback(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        JSMod* mod = get_js_mod(ctx);
        if (!mod)
            return JS_NewInt32(ctx, -1);
        if (mod->is_safe_mode())
            return JS_NewInt32(ctx, -1);
        if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Delegate))
            return JS_NewInt32(ctx, -1);

        if (argc < 3)
            return JS_ThrowTypeError(ctx,
                "BindDelegateCallback requires 3 arguments: delegateOwner, delegateName, callback");

        void* owner_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!owner_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject (delegate owner)");
        auto* owner = static_cast<Unreal::UObject*>(owner_ptr);

        const char* name_c = JS_ToCString(ctx, argv[1]);
        if (!name_c)
            return JS_ThrowTypeError(ctx, "Second argument must be a string (delegate name)");

        if (!JS_IsFunction(ctx, argv[2])) {
            JS_FreeCString(ctx, name_c);
            return JS_ThrowTypeError(ctx, "Third argument must be a function (callback)");
        }

        std::wstring wide_name = utf8_to_wide(std::string(name_c));
        JS_FreeCString(ctx, name_c);

        int64_t acquired_shared_hook_id = 0;
        bool delegate_registered = false;
        Unreal::FName registered_sig_func_name{};
        try
        {
            auto lookup = seh_find_delegate_property(owner, wide_name.c_str());
            if (!lookup.prop || !lookup.prop_value)
            {
                Output::send<LogLevel::Error>(
                    STR("[UE4SSL.JavaScript] BindDelegateCallback: delegate '{}' not found\n"), wide_name);
                mod->report_subsystem_failure(JSMod::GuardedSubsystem::Delegate, L"BindDelegateCallback", L"Delegate property not found");
                return JS_NewInt32(ctx, -1);
            }

            Unreal::UFunction* sig_func = seh_get_signature_function(lookup.prop);
            if (!sig_func)
            {
                Output::send<LogLevel::Error>(
                    STR("[UE4SSL.JavaScript] BindDelegateCallback: signature function not found for '{}'\n"),
                    wide_name);
                mod->report_subsystem_failure(JSMod::GuardedSubsystem::Delegate, L"BindDelegateCallback", L"Signature function not found");
                return JS_NewInt32(ctx, -1);
            }

            Unreal::FName sig_func_name = sig_func->GetNamePrivate();

            // Inject the signature function into the owner's class FuncMap.
            // Without this, owner->FindFunction(sigFuncName) returns null for
            // package-level signature functions, causing the delegate broadcast
            // to silently skip the binding.
            if (!seh_inject_into_funcmap(owner, sig_func_name, sig_func))
            {
                Output::send<LogLevel::Error>(
                    STR("[UE4SSL.JavaScript] BindDelegateCallback: failed to inject sigFunc into FuncMap\n"));
                mod->report_subsystem_failure(JSMod::GuardedSubsystem::Delegate, L"BindDelegateCallback", L"FuncMap injection failed");
                return JS_NewInt32(ctx, -1);
            }
            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] BindDelegateCallback: injected sigFunc '{}' into owner FuncMap\n"),
                sig_func_name.ToString());



            auto* shared_hook = acquire_shared_signature_hook(mod, ctx, sig_func);
            if (!shared_hook)
            {
                Output::send<LogLevel::Error>(
                    STR("[UE4SSL.JavaScript] BindDelegateCallback: failed to acquire shared signature hook\n"));
                mod->report_subsystem_failure(
                    JSMod::GuardedSubsystem::Delegate,
                    L"BindDelegateCallback",
                    L"Shared signature hook acquisition failed");
                return JS_NewInt32(ctx, -1);
            }
            acquired_shared_hook_id = shared_hook->id;
            registered_sig_func_name = sig_func_name;

            Unreal::FScriptDelegate sd;
            sd.BindUFunction(owner, sig_func_name);

            // Fix broken FWeakObjectPtr (serial=0) before any AddDelegate attempt
            seh_fix_weak_object_ptr(sd, owner);

            if (!seh_probe_uobject_alive(owner))
            {
                Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.JavaScript] BindDelegateCallback: owner UObject became invalid before AddDelegate; aborting bind for '{}'\n"),
                    wide_name);
                mod->report_subsystem_failure(
                    JSMod::GuardedSubsystem::Delegate,
                    L"BindDelegateCallback",
                    L"Owner UObject invalid pre-AddDelegate");
                if (acquired_shared_hook_id != 0)
                {
                    release_shared_signature_hook(acquired_shared_hook_id);
                }
                return JS_NewInt32(ctx, -1);
            }

            lookup.prop->AddDelegate(sd, owner, lookup.prop_value);
            seh_ensure_delegate_entry(lookup.prop, lookup.prop_value, owner, sig_func_name, sd);
            seh_count_delegate_entries(lookup.prop, lookup.prop_value, owner, sig_func_name);
            delegate_registered = true;

            {
                auto* vf = seh_find_in_funcmap(owner, sig_func_name);
                Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.JavaScript] BindDelegateCallback: FuncMap verify found={:016X} expected={:016X}\n"),
                    std::bit_cast<uintptr_t>(vf), std::bit_cast<uintptr_t>(sig_func));
                auto fp = seh_read_func_ptr(sig_func);
                if (fp.ok)
                    Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.JavaScript] BindDelegateCallback: sigFunc FuncPtr={:016X} FUNC_Native={}\n"),
                        fp.fptr, fp.is_native);
            }

            int callback_id = s_next_delegate_callback_id++;
            s_delegate_callbacks.push_back({
                callback_id,
                mod,
                ctx,
                JS_DupValue(ctx, argv[2]),
                owner,
                wide_name,
                sig_func_name,
                shared_hook->id,
            });

            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] BindDelegateCallback: bound '{}' (sigFunc='{}') -> callback id {}\n"),
                wide_name, sig_func->GetFullName(), callback_id);

            mod->report_subsystem_success(JSMod::GuardedSubsystem::Delegate);
            return JS_NewInt32(ctx, callback_id);
        }
        catch (const std::exception& e)
        {
            if (delegate_registered && seh_probe_uobject_alive(owner))
            {
                try
                {
                    auto lookup = seh_find_delegate_property(owner, wide_name.c_str());
                    if (lookup.prop && lookup.prop_value)
                    {
                        Unreal::FScriptDelegate sd;
                        sd.BindUFunction(owner, registered_sig_func_name);
                        lookup.prop->RemoveDelegate(sd, owner, lookup.prop_value);
                    }
                }
                catch (...) {}
            }
            if (acquired_shared_hook_id != 0)
            {
                release_shared_signature_hook(acquired_shared_hook_id);
            }
            Output::send<LogLevel::Error>(
                STR("[UE4SSL.JavaScript] BindDelegateCallback exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            mod->report_subsystem_failure(JSMod::GuardedSubsystem::Delegate, L"BindDelegateCallback", L"std::exception");
            return JS_NewInt32(ctx, -1);
        }
        catch (...)
        {
            if (delegate_registered && seh_probe_uobject_alive(owner))
            {
                try
                {
                    auto lookup = seh_find_delegate_property(owner, wide_name.c_str());
                    if (lookup.prop && lookup.prop_value)
                    {
                        Unreal::FScriptDelegate sd;
                        sd.BindUFunction(owner, registered_sig_func_name);
                        lookup.prop->RemoveDelegate(sd, owner, lookup.prop_value);
                    }
                }
                catch (...) {}
            }
            if (acquired_shared_hook_id != 0)
            {
                release_shared_signature_hook(acquired_shared_hook_id);
            }
            Output::send<LogLevel::Error>(
                STR("[UE4SSL.JavaScript] BindDelegateCallback unknown exception\n"));
            mod->report_subsystem_failure(JSMod::GuardedSubsystem::Delegate, L"BindDelegateCallback", L"unknown exception");
            return JS_NewInt32(ctx, -1);
        }
    }

    // UnbindDelegateCallback(callbackId) -> bool

    JSValue js_unbind_delegate_callback(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        JSMod* mod = get_js_mod(ctx);
        if (argc < 1)
            return JS_ThrowTypeError(ctx,
                "UnbindDelegateCallback requires 1 argument: callbackId");

        int callback_id;
        if (JS_ToInt32(ctx, &callback_id, argv[0]))
            return JS_NewBool(ctx, false);

        for (auto it = s_delegate_callbacks.begin(); it != s_delegate_callbacks.end(); ++it)
        {
            if (it->id != callback_id) continue;

            if (!seh_probe_uobject_alive(it->owner))
            {
                Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.JavaScript] UnbindDelegateCallback: owner UObject is stale, skipping RemoveDelegate for '{}'\n"),
                    it->delegate_name);
            }
            else
            {
                try
                {
                    auto lookup = seh_find_delegate_property(it->owner, it->delegate_name.c_str());
                    if (lookup.prop && lookup.prop_value)
                    {
                        Unreal::FScriptDelegate sd;
                        sd.BindUFunction(it->owner, it->sig_func_name);
                        lookup.prop->RemoveDelegate(sd, it->owner, lookup.prop_value);
                    }
                }
                catch (...) {}
            }

            if (it->ctx && !JS_IsUndefined(it->callback))
            {
                JS_FreeValue(it->ctx, it->callback);
                it->callback = JS_UNDEFINED;
            }

            release_shared_signature_hook(it->shared_hook_id);

            s_delegate_callbacks.erase(it);

            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] UnbindDelegateCallback: removed callback {}\n"), callback_id);
            if (mod) mod->report_subsystem_success(JSMod::GuardedSubsystem::Delegate);
            return JS_NewBool(ctx, true);
        }

        if (mod) mod->report_subsystem_failure(JSMod::GuardedSubsystem::Delegate, L"UnbindDelegateCallback", L"Callback id not found");
        return JS_NewBool(ctx, false);
    }

} // namespace RC::JSScript
