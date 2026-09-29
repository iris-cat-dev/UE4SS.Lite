#include "JSInternal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UE4SSProgram.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

#ifndef UE4SS_CONFIGURATION
#define UE4SS_CONFIGURATION "Unknown"
#endif

extern "C"
{
    size_t ue4ssl_js_global_api_count();
    const char* ue4ssl_js_global_api_name(size_t index);
    int ue4ssl_js_global_api_arity(size_t index);
    bool ue4ssl_js_global_api_is_function(size_t index);
}

namespace RC::JSScript
{
    static constexpr std::array<JSCFunction*, 59> GlobalFunctionImplementations{
            js_print,
            js_find_first_instance_of_class,
            js_find_all_instances_of_class,
            js_find_first_of,
            js_find_all_of,
            js_find_all_actors_with_interface,
            js_static_find_object,
            js_load_object,
            js_scan_blueprint_widgets_by_interface,
            js_register_hook,
            js_register_bind_hook,
            js_register_native_object_method_hook,
            js_register_load_map_pre_hook,
            js_register_load_map_post_hook,
            js_hook_ufunction,
            js_unregister_hook,
            js_unregister_bind_hook,
            js_unregister_load_map_hook,
            js_notify_on_new_object,
            js_register_key_bind,
            js_call_function,
            js_call_function_ex,
            js_with_exec_budget,
            js_set_timeout,
            js_set_interval,
            js_clear_timeout,
            js_clear_interval,
            js_fetch,
            js_fetch_sync,
            js_sig_scan,
            js_patch_byte,
            js_read_byte,
            js_read_file,
            js_write_file,
            js_get_mods_directory,
            js_get_game_directory,
            js_download_file,
            js_download_file_sync,
            js_play_sound_file,
            js_stop_sound,
            js_get_property,
            js_set_property,
            js_export_property_text,
            js_get_property_path,
            js_set_property_path,
            js_apply_object_patch,
            js_bind_delegate,
            js_unbind_delegate,
            js_clear_delegate,
            js_register_pe_watch,
            js_bind_delegate_callback,
            js_unbind_delegate_callback,
            js_new_uobject,
            js_umg_dispatch_sync,
            js_umg_dispatch_async,
            js_umg_create_user_widget,
            js_umg_clone_user_widget,
            js_umg_set_user_widget_root,
            js_umg_construct_widget,
    };

    static auto subsystem_to_name(JSMod::GuardedSubsystem subsystem) -> const wchar_t*
    {
        switch (subsystem)
        {
            case JSMod::GuardedSubsystem::Hook: return L"Hook";
            case JSMod::GuardedSubsystem::Timer: return L"Timer";
            case JSMod::GuardedSubsystem::Fetch: return L"Fetch";
            case JSMod::GuardedSubsystem::Delegate: return L"Delegate";
            default: return L"Unknown";
        }
    }

    static auto safe_stop_unregister_ufunction_hook(Unreal::UFunction* function,
                                                    Unreal::CallbackId pre_id,
                                                    Unreal::CallbackId post_id) -> bool
    {
        if (!function || (pre_id == 0 && post_id == 0))
        {
            return false;
        }

        __try
        {
            Unreal::UObjectGlobals::UnregisterHook(function, {pre_id, post_id});
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Output::send<LogLevel::Warning>(
                STR("[UE4SSL.JavaScript] stop(): UFunction pointer is stale during unregister, skipping engine unregister\n"));
            return false;
        }
    }

    static auto js_value_to_utf8_string(JSContext* ctx, JSValueConst value) -> std::string
    {
        size_t len = 0;
        const char* cstr = JS_ToCStringLen(ctx, &len, value);
        if (!cstr)
        {
            return {};
        }

        std::string out(cstr, cstr + len);
        JS_FreeCString(ctx, cstr);
        return out;
    }

    static auto js_current_script_name_utf8(JSContext* ctx) -> std::string
    {
        JSAtom script_atom = JS_GetScriptOrModuleName(ctx, 0);
        if (script_atom == JS_ATOM_NULL)
        {
            return {};
        }

        size_t len = 0;
        const char* script_cstr = JS_AtomToCStringLen(ctx, &len, script_atom);
        std::string script_name = script_cstr ? std::string(script_cstr, script_cstr + len) : std::string{};
        if (script_cstr)
        {
            JS_FreeCString(ctx, script_cstr);
        }
        JS_FreeAtom(ctx, script_atom);
        return script_name;
    }

    static int seh_tick_impl(JSMod* mod)
    {
        __try
        {
            mod->tick_impl();
            return 1;
        }
        __except (Seh::FilterAndLog(L"JavaScript", L"tick_impl", GetExceptionCode(), GetExceptionInformation()))
        {
            return 0;
        }
    }

    static int js_runtime_interrupt_handler(JSRuntime* /*rt*/, void* opaque)
    {
        auto* mod = static_cast<JSMod*>(opaque);
        if (!mod)
        {
            return 0;
        }
        return mod->should_interrupt_execution() ? 1 : 0;
    }

    static void js_host_promise_rejection_tracker(JSContext* ctx,
                                                  JSValueConst /*promise*/,
                                                  JSValueConst reason,
                                                  bool is_handled,
                                                  void* opaque)
    {
        auto* mod = static_cast<JSMod*>(opaque);
        if (!mod || is_handled)
        {
            return;
        }

        mod->note_unhandled_promise_rejection();

        std::string reason_utf8 = js_value_to_utf8_string(ctx, reason);
        if (reason_utf8.empty())
        {
            reason_utf8 = "(unknown reason)";
        }

        log_stability_error(L"E_UNHANDLED_PROMISE_REJECTION",
                            L"Runtime",
                            L"PromiseRejectionTracker",
                            utf8_to_wide(reason_utf8));
    }

    // ============================================
    // Constructor / Destructor
    // ============================================

    JSMod::JSMod()
    {
        auto& program = UE4SSProgram::get_program();
        m_mods_directory = program.get_mods_directory();

        auto now = std::chrono::steady_clock::now();
        m_start_time = std::chrono::duration<double>(now.time_since_epoch()).count();

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Mods directory: {}\n"), m_mods_directory.wstring());
    }

    JSMod::~JSMod()
    {
        stop();
    }

    // ============================================
    // Lifecycle
    // ============================================

    auto JSMod::start() -> bool
    {
        if (!init_engine())
        {
            return false;
        }
        load_scripts();
        return true;
    }

    auto JSMod::init_engine() -> bool
    {
        if (m_initialized)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Already initialized\n"));
            return true;
        }

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Starting JavaScript engine...\n"));

        if (!init_runtime())
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Failed to initialize runtime\n"));
            return false;
        }

        if (!init_context())
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Failed to initialize context\n"));
            stop();
            return false;
        }

        setup_module_loader();

        m_initialized = true;
        m_event_loop_thread_id = std::this_thread::get_id();
        return true;
    }

    auto JSMod::load_scripts() -> bool
    {
        if (!m_initialized)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Engine not initialized, cannot load scripts\n"));
            return false;
        }

        auto scripts = find_scripts();
        if (scripts.empty())
        {
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] No scripts found in {}\n"), m_mods_directory.wstring());
            return false;
        }

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Found {} script(s)\n"), scripts.size());
        for (const auto& script : scripts)
        {
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Loading script: {}\n"), script.filename().wstring());
            load_and_execute_script(script);
        }

        setup_game_thread_dispatcher();
        setup_umg_dispatcher();
        return true;
    }

    auto JSMod::stop() -> void
    {
        UE4SSProgram::get_program().unregister_input_owner(reinterpret_cast<uintptr_t>(this));
        // Input dispatch is drained first; the VM lock then waits for dequeued keybinds.
        std::lock_guard<std::recursive_mutex> js_lock(m_js_mutex);
        if (!m_initialized && !m_runtime)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_pending_keybind_mutex);
            m_pending_keybind_callbacks.clear();
        }

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Stopping JavaScript engine...\n"));

        clear_bind_hook_state();
        clear_load_map_hook_state();

        {
            std::lock_guard<std::mutex> lock(m_ufunction_hooks_mutex);
            for (auto& hook : m_ufunction_hooks)
            {
                if (!hook)
                {
                    continue;
                }

                if (hook->function)
                {
                    (void)safe_stop_unregister_ufunction_hook(hook->function, hook->pre_id, hook->post_id);
                    hook->function = nullptr;
                }

                if (hook->ctx)
                {
                    if (!JS_IsUndefined(hook->pre_callback))
                        JS_FreeValue(hook->ctx, hook->pre_callback);
                    if (!JS_IsUndefined(hook->post_callback))
                        JS_FreeValue(hook->ctx, hook->post_callback);
                }
            }
            m_ufunction_hooks.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_native_method_hooks_mutex);
            for (auto& hook : m_native_method_hooks)
            {
                if (!hook)
                {
                    continue;
                }
                safe_stop_unregister_ufunction_hook(hook->hook_function, hook->pre_id, hook->post_id);
            }
            m_native_method_hooks.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_hooks_mutex);
            for (auto& hook : m_hook_callbacks)
            {
                if (hook.ctx)
                    JS_FreeValue(hook.ctx, hook.callback);
            }
            m_hook_callbacks.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_timers_mutex);
            for (auto& timer : m_timers)
            {
                if (timer.ctx)
                    JS_FreeValue(timer.ctx, timer.callback);
            }
            m_timers.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_key_bindings_mutex);
            for (auto& key_bind : m_key_bindings)
            {
                if (key_bind && key_bind->ctx)
                    JS_FreeValue(key_bind->ctx, key_bind->callback);
            }
            m_key_bindings.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_pending_game_thread_mutex);
            for (auto& call : m_pending_game_thread_calls)
            {
                for (auto* buf : call.raw_string_buffers) { free(buf); }
                if (call.params_memory) { free(call.params_memory); }
            }
            m_pending_game_thread_calls.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_pending_hook_callbacks_mutex);
            m_pending_hook_callbacks.clear();
        }

        m_fetch_worker_stop.store(true);
        m_fetch_request_cv.notify_all();
        if (m_fetch_worker.joinable())
            m_fetch_worker.join();
        process_fetch_results();
        {
            std::lock_guard<std::mutex> lock(m_fetch_mutex);
            for (auto& kv : m_fetch_pending)
            {
                if (m_main_ctx)
                {
                    JS_FreeValue(m_main_ctx, kv.second.resolve_func);
                    JS_FreeValue(m_main_ctx, kv.second.reject_func);
                }
            }
            m_fetch_pending.clear();
            for (auto& kv : m_fetch_streams)
            {
                if (m_main_ctx)
                {
                    for (auto& read_cb : kv.second.pending_reads)
                    {
                        JS_FreeValue(m_main_ctx, read_cb.resolve_func);
                        JS_FreeValue(m_main_ctx, read_cb.reject_func);
                    }
                }
                kv.second.pending_reads.clear();
            }
            m_fetch_streams.clear();
            m_fetch_request_queue.clear();
            m_fetch_result_queue.clear();
        }

        m_download_worker_stop.store(true);
        m_download_request_cv.notify_all();
        if (m_download_worker.joinable())
            m_download_worker.join();
        process_download_results();
        {
            std::lock_guard<std::mutex> lock(m_download_mutex);
            for (auto& kv : m_download_pending)
            {
                if (m_main_ctx)
                {
                    JS_FreeValue(m_main_ctx, kv.second.resolve_func);
                    JS_FreeValue(m_main_ctx, kv.second.reject_func);
                }
            }
            m_download_pending.clear();
            m_download_request_queue.clear();
            m_download_result_queue.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_pe_watches_mutex);
            for (auto& watch : m_pe_watches)
            {
                if (m_main_ctx && !JS_IsUndefined(watch.callback))
                    JS_FreeValue(m_main_ctx, watch.callback);
            }
            m_pe_watches.clear();
        }

        reset_param_ref_class(m_main_ctx);
        reset_delegate_statics();

        if (m_main_ctx)
        {
            JS_FreeContext(m_main_ctx);
            m_main_ctx = nullptr;
        }

        if (m_runtime)
        {
            JS_FreeRuntime(m_runtime);
            m_runtime = nullptr;
        }

        m_initialized = false;
        m_loaded_modules.clear();
        m_in_tick = false;
        m_tick_seh_failures = 0;
        m_exec_budget_active.store(false);
        m_exec_budget_deadline_us.store(0);
        m_unhandled_promise_rejections.store(0);
        m_safe_mode.store(false);
        {
            std::lock_guard<std::mutex> lock(m_subsystem_circuit_mutex);
            for (auto& state : m_subsystem_circuit_states)
            {
                state = {};
            }
        }

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] JavaScript engine stopped\n"));
    }

    // ============================================
    // Tick - SEH-protected main event loop
    // ============================================

    static void tick_inner_keybinds(JSMod* self)
    {
        std::vector<JSMod::KeyBindCallback*> to_execute;
        {
            std::lock_guard<std::mutex> lock(self->m_pending_keybind_mutex);
            to_execute.swap(self->m_pending_keybind_callbacks);
        }

        if (!to_execute.empty())
        {
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] [DIAG] tick_inner_keybinds: DEQUEUED {} keybind(s) for execution\n"), to_execute.size());
        }

        for (auto* key_bind : to_execute)
        {
            if (!key_bind || !key_bind->ctx) continue;
            if (key_bind->is_executing) continue;
            key_bind->is_executing = true;

            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] [DIAG] Executing keybind callback for key 0x{:X}...\n"), key_bind->key);

            self->end_exec_budget();

            auto t_start = std::chrono::steady_clock::now();
            JSValue result = JS_UNDEFINED;
            if (safe_js_call(key_bind->ctx, key_bind->callback, JS_UNDEFINED, 0, nullptr, &result))
            {
                if (JS_IsException(result))
                {
                    self->log_exception(key_bind->ctx, L"KeyBindCallback");
                    self->report_subsystem_failure(JSMod::GuardedSubsystem::Hook, L"KeyBindCallback", L"JS exception");
                }
                else
                {
                    self->report_subsystem_success(JSMod::GuardedSubsystem::Hook);
                }
                JS_FreeValue(key_bind->ctx, result);
            }
            else
            {
                self->report_subsystem_failure(JSMod::GuardedSubsystem::Hook, L"KeyBindCallback", L"SEH exception");
            }

            auto t_end = std::chrono::steady_clock::now();
            double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] [DIAG] Keybind callback for key 0x{:X} finished in {:.1f}ms\n"), key_bind->key, elapsed_ms);

            key_bind->is_executing = false;

            self->begin_exec_budget();
        }
    }

    static void tick_inner_pending_hooks(JSMod* self)
    {
        if (self->is_subsystem_disabled(JSMod::GuardedSubsystem::Hook))
        {
            std::lock_guard<std::mutex> lock(self->m_pending_hook_callbacks_mutex);
            self->m_pending_hook_callbacks.clear();
            return;
        }

        std::vector<JSMod::PendingHookCallback> hooks_to_execute;
        {
            std::lock_guard<std::mutex> lock(self->m_pending_hook_callbacks_mutex);
            hooks_to_execute.swap(self->m_pending_hook_callbacks);
        }

        for (auto& pending : hooks_to_execute)
        {
            if (!pending.hook_data) continue;

            auto raw_ptr = reinterpret_cast<uintptr_t>(pending.hook_data);
            bool is_pe_watch = (raw_ptr & 0x8000000000000000ULL) != 0;

            JSContext* ctx = nullptr;
            JSValue callback = JS_UNDEFINED;

            if (is_pe_watch)
            {
                size_t watch_idx = static_cast<size_t>(raw_ptr & 0x7FFFFFFFFFFFFFFFULL);
                std::lock_guard<std::mutex> lock(self->m_pe_watches_mutex);
                if (watch_idx >= self->m_pe_watches.size()) continue;
                auto& watch = self->m_pe_watches[watch_idx];
                if (JS_IsUndefined(watch.callback)) continue;
                ctx = self->get_main_context();
                callback = watch.callback;
            }
            else
            {
                bool hook_still_valid = false;
                {
                    std::lock_guard<std::mutex> lock(self->m_ufunction_hooks_mutex);
                    for (auto& h : self->m_ufunction_hooks)
                    {
                        if (h.get() == pending.hook_data) { hook_still_valid = true; break; }
                    }
                }
                if (!hook_still_valid || !pending.hook_data->ctx) continue;
                ctx = pending.hook_data->ctx;
                callback = pending.is_pre ? pending.hook_data->pre_callback : pending.hook_data->post_callback;
            }

            if (!ctx || JS_IsUndefined(callback)) continue;

            JSValue args[3];
            args[0] = pending.context_object
                ? JSUObject::create(ctx, pending.context_object)
                : JS_NULL;

            JSValue js_params = JS_NewArray(ctx);
            for (size_t i = 0; i < pending.params.size(); i++)
            {
                JSValue param_val;
                switch (pending.params[i].type)
                {
                    case JSMod::PendingHookCallbackParam::Type::String: {
                        std::string utf8 = wide_to_utf8(pending.params[i].str_val);
                        param_val = JS_NewString(ctx, utf8.c_str());
                        break;
                    }
                    case JSMod::PendingHookCallbackParam::Type::StringArray: {
                        param_val = JS_NewArray(ctx);
                        for (size_t j = 0; j < pending.params[i].str_array_val.size(); j++)
                        {
                            std::string utf8 = wide_to_utf8(pending.params[i].str_array_val[j]);
                            JS_SetPropertyUint32(ctx, param_val, static_cast<uint32_t>(j), JS_NewString(ctx, utf8.c_str()));
                        }
                        break;
                    }
                    case JSMod::PendingHookCallbackParam::Type::Int:
                        param_val = JS_NewInt64(ctx, pending.params[i].int_val);
                        break;
                    case JSMod::PendingHookCallbackParam::Type::Float:
                        param_val = JS_NewFloat64(ctx, pending.params[i].float_val);
                        break;
                    case JSMod::PendingHookCallbackParam::Type::Bool:
                        param_val = JS_NewBool(ctx, pending.params[i].bool_val);
                        break;
                    case JSMod::PendingHookCallbackParam::Type::Object:
                        param_val = pending.params[i].obj_val
                            ? JSUObject::create(ctx, pending.params[i].obj_val)
                            : JS_NULL;
                        break;
                    case JSMod::PendingHookCallbackParam::Type::Json: {
                        std::string utf8 = wide_to_utf8(pending.params[i].str_val);
                        if (utf8.empty())
                        {
                            param_val = JS_NULL;
                            break;
                        }

                        param_val = JS_ParseJSON(ctx, utf8.c_str(), utf8.size(), "<deferred-hook-param>");
                        if (JS_IsException(param_val))
                        {
                            JS_FreeValue(ctx, param_val);
                            param_val = JS_NULL;
                        }
                        break;
                    }
                    default:
                        param_val = JS_NULL;
                        break;
                }
                JS_SetPropertyUint32(ctx, js_params, static_cast<uint32_t>(i), param_val);
            }
            args[1] = js_params;
            args[2] = JS_UNDEFINED;

            JSValue result = JS_UNDEFINED;
            if (safe_js_call(ctx, callback, JS_UNDEFINED, 3, args, &result))
            {
                if (JS_IsException(result))
                {
                    self->log_exception(ctx, L"DeferredHookCallback");
                    self->report_subsystem_failure(JSMod::GuardedSubsystem::Hook, L"DeferredHookCallback", L"JS exception");
                }
                else
                {
                    self->report_subsystem_success(JSMod::GuardedSubsystem::Hook);
                }
                JS_FreeValue(ctx, result);
            }
            else
            {
                self->report_subsystem_failure(JSMod::GuardedSubsystem::Hook, L"DeferredHookCallback", L"SEH exception");
            }

            JS_FreeValue(ctx, args[0]);
            JS_FreeValue(ctx, args[1]);
        }
    }

    static int safe_copy_fstring_to_wstring(Unreal::FString* source, std::wstring& out_value)
    {
        out_value.clear();
        if (!source)
        {
            return 0;
        }

        __try
        {
            int32_t num = source->GetCharArray().Num();
            if (num <= 1)
            {
                return 0;
            }

            const wchar_t* data = source->GetCharArray().GetData();
            if (!data)
            {
                return 0;
            }

            volatile wchar_t probe = *data;
            (void)probe;

            out_value.assign(data, num - 1);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out_value.clear();
            return 0;
        }
    }

    auto JSMod::tick_impl() -> void
    {
        try { tick_inner_keybinds(this); }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] tick_inner_keybinds exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        catch (...) { Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] tick_inner_keybinds unknown exception\n")); }

        try { process_pending_bind_hook_activations(); }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_pending_bind_hook_activations exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        catch (...) { Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_pending_bind_hook_activations unknown exception\n")); }

        try { tick_inner_pending_hooks(this); }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] tick_inner_pending_hooks exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        catch (...) { Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] tick_inner_pending_hooks unknown exception\n")); }

        try { process_pending_load_map_events(); }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_pending_load_map_events exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        catch (...) { Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_pending_load_map_events unknown exception\n")); }

        try { process_timers(); }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_timers exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        catch (...) { Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_timers unknown exception\n")); }

        try { process_fetch_results(); }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_fetch_results exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        catch (...) { Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_fetch_results unknown exception\n")); }

        try { process_download_results(); }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_download_results exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        catch (...) { Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_download_results unknown exception\n")); }

        JSContext* ctx = nullptr;
        int err = 0;
        uint32_t jobs_executed = 0;
        bool budget_hit = false;
        const double job_loop_start = get_current_time();
        while (true)
        {
            if (jobs_executed >= m_tick_job_budget)
            {
                budget_hit = true;
                break;
            }

            if (((get_current_time() - job_loop_start) * 1000.0) >= m_tick_time_budget_ms)
            {
                budget_hit = true;
                break;
            }

            err = safe_js_execute_pending_job(m_runtime, &ctx);
            if (err <= 0)
            {
                break;
            }

            jobs_executed++;
        }

        const bool pending_after_budget = budget_hit && JS_IsJobPending(m_runtime);
        if (pending_after_budget)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Pending job budget hit (jobs={}, budget={}, time_budget_ms={})\n"),
                jobs_executed, m_tick_job_budget, m_tick_time_budget_ms);
        }

        if (err == -2)
        {
            report_subsystem_failure(GuardedSubsystem::Hook, L"ExecutePendingJob", L"SEH exception");
        }
        else if (err < 0)
        {
            const bool budget_interrupt = log_exception(ctx ? ctx : m_main_ctx, L"ExecutePendingJob");
            if (budget_interrupt)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] ExecutePendingJob interrupted by execution budget; treating as pressure, not a Hook failure\n"));
            }
            else
            {
                report_subsystem_failure(GuardedSubsystem::Hook, L"ExecutePendingJob", L"JS exception");
            }
        }
        else if (pending_after_budget)
        {
            // A full pending-job queue under the time budget is backpressure, not proof that
            // the Hook subsystem recovered. Leave circuit-breaker failure counters unchanged.
        }
        else
        {
            report_subsystem_success(GuardedSubsystem::Hook);
        }
    }

    auto JSMod::tick() -> void
    {
        std::lock_guard<std::recursive_mutex> js_lock(m_js_mutex);
        if (!m_initialized || !m_main_ctx)
            return;

        if (m_in_tick)
            return;

        m_in_tick = true;
        JS_UpdateStackTop(m_runtime);
        begin_exec_budget();

        const bool ok = seh_tick_impl(this) != 0;

        end_exec_budget();
        m_in_tick = false;

        if (!ok)
        {
            m_tick_seh_failures++;
            log_stability_error(L"E_TICK_SEH", L"Runtime", L"TickLoop",
                L"SEH exception escaped tick_impl and was isolated", Seh::GetLastSehCode());
            if (m_tick_seh_failures >= 3)
            {
                enter_safe_mode(L"Tick loop encountered repeated SEH exceptions");
            }
        }
        else
        {
            m_tick_seh_failures = 0;
        }
    }

    auto JSMod::ensure_load_map_callbacks_registered() -> bool
    {
        if (m_load_map_pre_callback_id != Unreal::Hook::ERROR_ID &&
            m_load_map_post_callback_id != Unreal::Hook::ERROR_ID)
        {
            return true;
        }

        const Unreal::Hook::FCallbackOptions common_opts{false, false, STR("UE4SSL.JavaScript"), STR("JavaScriptLoadMap")};

        if (m_load_map_pre_callback_id == Unreal::Hook::ERROR_ID)
        {
            m_load_map_pre_callback_id = Unreal::Hook::RegisterLoadMapPreCallback(
                [this](Unreal::Hook::TCallbackIterationData<bool>&,
                       Unreal::UEngine*,
                       Unreal::FWorldContext&,
                       Unreal::FURL URL,
                       Unreal::UPendingNetGame* PendingGame,
                       Unreal::FString& Error)
                {
                    if (!this)
                    {
                        return;
                    }

                    PendingLoadMapEvent event{};
                    event.is_pre = true;
                    event.has_pending_net_game = PendingGame != nullptr;
                    (void)safe_copy_fstring_to_wstring(&URL.Map, event.map);
                    (void)safe_copy_fstring_to_wstring(&URL.Host, event.host);
                    (void)safe_copy_fstring_to_wstring(&URL.Portal, event.portal);
                    (void)safe_copy_fstring_to_wstring(&URL.RedirectURL, event.redirect_url);
                    (void)safe_copy_fstring_to_wstring(&Error, event.error);

                    std::lock_guard<std::mutex> lock(m_pending_load_map_events_mutex);
                    m_pending_load_map_events.push_back(std::move(event));
                },
                common_opts);
        }

        if (m_load_map_post_callback_id == Unreal::Hook::ERROR_ID)
        {
            m_load_map_post_callback_id = Unreal::Hook::RegisterLoadMapPostCallback(
                [this](Unreal::Hook::TCallbackIterationData<bool>&,
                       Unreal::UEngine*,
                       Unreal::FWorldContext&,
                       Unreal::FURL URL,
                       Unreal::UPendingNetGame* PendingGame,
                       Unreal::FString& Error)
                {
                    if (!this)
                    {
                        return;
                    }

                    PendingLoadMapEvent event{};
                    event.is_pre = false;
                    event.has_pending_net_game = PendingGame != nullptr;
                    (void)safe_copy_fstring_to_wstring(&URL.Map, event.map);
                    (void)safe_copy_fstring_to_wstring(&URL.Host, event.host);
                    (void)safe_copy_fstring_to_wstring(&URL.Portal, event.portal);
                    (void)safe_copy_fstring_to_wstring(&URL.RedirectURL, event.redirect_url);
                    (void)safe_copy_fstring_to_wstring(&Error, event.error);

                    std::lock_guard<std::mutex> lock(m_pending_load_map_events_mutex);
                    m_pending_load_map_events.push_back(std::move(event));
                },
                common_opts);
        }

        if (m_load_map_pre_callback_id == Unreal::Hook::ERROR_ID ||
            m_load_map_post_callback_id == Unreal::Hook::ERROR_ID)
        {
            Output::send<LogLevel::Warning>(
                STR("[UE4SSL.JavaScript] Failed to register LoadMap callback bridge (pre_id={}, post_id={})\n"),
                m_load_map_pre_callback_id,
                m_load_map_post_callback_id);
            return false;
        }

        Output::send<LogLevel::Normal>(
            STR("[UE4SSL.JavaScript] LoadMap callback bridge registered (pre_id={}, post_id={})\n"),
            m_load_map_pre_callback_id,
            m_load_map_post_callback_id);
        return true;
    }

    auto JSMod::register_load_map_hook(JSContext* ctx, bool is_pre, JSValue callback) -> int32_t
    {
        if (!ensure_load_map_callbacks_registered())
        {
            return 0;
        }

        LoadMapCallbackRegistration registration{};
        registration.id = m_next_load_map_callback_id++;
        registration.ctx = ctx;
        registration.callback = JS_DupValue(ctx, callback);
        registration.is_pre = is_pre;

        {
            std::lock_guard<std::mutex> lock(m_load_map_callbacks_mutex);
            m_load_map_callbacks.push_back(registration);
        }

        Output::send<LogLevel::Normal>(
            STR("[UE4SSL.JavaScript] Registered LoadMap {} callback (id={})\n"),
            is_pre ? STR("pre") : STR("post"),
            registration.id);
        return registration.id;
    }

    auto JSMod::unregister_load_map_hook(int32_t id) -> bool
    {
        std::lock_guard<std::mutex> lock(m_load_map_callbacks_mutex);
        for (auto it = m_load_map_callbacks.begin(); it != m_load_map_callbacks.end(); ++it)
        {
            if (it->id != id)
            {
                continue;
            }

            if (it->ctx && !JS_IsUndefined(it->callback))
            {
                JS_FreeValue(it->ctx, it->callback);
            }

            m_load_map_callbacks.erase(it);
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Unregistered LoadMap callback (id={})\n"), id);
            return true;
        }

        return false;
    }

    auto JSMod::process_pending_load_map_events() -> void
    {
        if (is_subsystem_disabled(GuardedSubsystem::Hook))
        {
            std::lock_guard<std::mutex> lock(m_pending_load_map_events_mutex);
            m_pending_load_map_events.clear();
            return;
        }

        std::vector<PendingLoadMapEvent> events{};
        {
            std::lock_guard<std::mutex> lock(m_pending_load_map_events_mutex);
            events.swap(m_pending_load_map_events);
        }

        if (events.empty())
        {
            return;
        }

        struct CallbackInvocation
        {
            int32_t id{0};
            JSContext* ctx{nullptr};
            JSValue callback{JS_UNDEFINED};
        };

        for (auto& event : events)
        {
            std::vector<CallbackInvocation> callbacks{};
            {
                std::lock_guard<std::mutex> lock(m_load_map_callbacks_mutex);
                callbacks.reserve(m_load_map_callbacks.size());
                for (const auto& registration : m_load_map_callbacks)
                {
                    if (registration.is_pre != event.is_pre || !registration.ctx || JS_IsUndefined(registration.callback))
                    {
                        continue;
                    }

                    callbacks.push_back({
                        registration.id,
                        registration.ctx,
                        JS_DupValue(registration.ctx, registration.callback),
                    });
                }
            }

            for (auto& invocation : callbacks)
            {
                JSValue event_object = JS_NewObject(invocation.ctx);
                JS_SetPropertyStr(invocation.ctx, event_object, "phase",
                    JS_NewString(invocation.ctx, event.is_pre ? "pre" : "post"));
                JS_SetPropertyStr(invocation.ctx, event_object, "map",
                    JS_NewString(invocation.ctx, wide_to_utf8(event.map).c_str()));
                JS_SetPropertyStr(invocation.ctx, event_object, "host",
                    JS_NewString(invocation.ctx, wide_to_utf8(event.host).c_str()));
                JS_SetPropertyStr(invocation.ctx, event_object, "portal",
                    JS_NewString(invocation.ctx, wide_to_utf8(event.portal).c_str()));
                JS_SetPropertyStr(invocation.ctx, event_object, "redirectUrl",
                    JS_NewString(invocation.ctx, wide_to_utf8(event.redirect_url).c_str()));
                JS_SetPropertyStr(invocation.ctx, event_object, "error",
                    JS_NewString(invocation.ctx, wide_to_utf8(event.error).c_str()));
                JS_SetPropertyStr(invocation.ctx, event_object, "hasPendingNetGame",
                    JS_NewBool(invocation.ctx, event.has_pending_net_game));

                JSValue args[1];
                args[0] = event_object;

                JSValue result = JS_UNDEFINED;
                if (safe_js_call(invocation.ctx, invocation.callback, JS_UNDEFINED, 1, args, &result))
                {
                    if (JS_IsException(result))
                    {
                        std::wstring callback_detail = std::wstring(L"id=")
                            + std::to_wstring(invocation.id)
                            + L", phase="
                            + (event.is_pre ? L"pre" : L"post");
                        log_exception(
                            invocation.ctx,
                            L"LoadMapCallback",
                            callback_detail);
                        report_subsystem_failure(GuardedSubsystem::Hook, L"LoadMapCallback", L"JS exception");
                    }
                    else
                    {
                        report_subsystem_success(GuardedSubsystem::Hook);
                    }
                    JS_FreeValue(invocation.ctx, result);
                }
                else
                {
                    report_subsystem_failure(GuardedSubsystem::Hook, L"LoadMapCallback", L"SEH exception");
                }

                JS_FreeValue(invocation.ctx, event_object);
                JS_FreeValue(invocation.ctx, invocation.callback);
            }
        }
    }

    auto JSMod::clear_load_map_hook_state() -> void
    {
        {
            std::lock_guard<std::mutex> lock(m_pending_load_map_events_mutex);
            m_pending_load_map_events.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_load_map_callbacks_mutex);
            for (auto& callback : m_load_map_callbacks)
            {
                if (callback.ctx && !JS_IsUndefined(callback.callback))
                {
                    JS_FreeValue(callback.ctx, callback.callback);
                }
            }
            m_load_map_callbacks.clear();
        }

        if (m_load_map_pre_callback_id != Unreal::Hook::ERROR_ID)
        {
            (void)Unreal::Hook::UnregisterCallback(m_load_map_pre_callback_id);
            m_load_map_pre_callback_id = Unreal::Hook::ERROR_ID;
        }

        if (m_load_map_post_callback_id != Unreal::Hook::ERROR_ID)
        {
            (void)Unreal::Hook::UnregisterCallback(m_load_map_post_callback_id);
            m_load_map_post_callback_id = Unreal::Hook::ERROR_ID;
        }
    }

    auto JSMod::get_current_time() const -> double
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now.time_since_epoch()).count();
    }

    auto JSMod::is_subsystem_disabled(GuardedSubsystem subsystem) -> bool
    {
        const auto idx = static_cast<size_t>(subsystem);
        if (idx >= m_subsystem_circuit_states.size())
        {
            return true;
        }
        std::lock_guard<std::mutex> lock(m_subsystem_circuit_mutex);
        return m_subsystem_circuit_states[idx].disabled;
    }

    auto JSMod::report_subsystem_success(GuardedSubsystem subsystem) -> void
    {
        const auto idx = static_cast<size_t>(subsystem);
        if (idx >= m_subsystem_circuit_states.size())
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_subsystem_circuit_mutex);
            auto& state = m_subsystem_circuit_states[idx];
            if (!state.disabled)
            {
                state.consecutive_failures = 0;
            }
        }
    }

    auto JSMod::report_subsystem_failure(GuardedSubsystem subsystem, const wchar_t* operation, const std::wstring& detail) -> void
    {
        const auto idx = static_cast<size_t>(subsystem);
        if (idx >= m_subsystem_circuit_states.size())
        {
            return;
        }

        bool trip_circuit = false;
        uint32_t failures = 0;
        {
            std::lock_guard<std::mutex> lock(m_subsystem_circuit_mutex);
            auto& state = m_subsystem_circuit_states[idx];
            if (state.disabled)
            {
                return;
            }
            state.consecutive_failures++;
            failures = state.consecutive_failures;
            if (state.consecutive_failures >= m_subsystem_failure_threshold)
            {
                state.disabled = true;
                trip_circuit = true;
            }
        }

        if (trip_circuit)
        {
            std::wstring reason = std::wstring(L"Subsystem ")
                + subsystem_to_name(subsystem)
                + L" reached failure threshold ("
                + std::to_wstring(failures)
                + L")";
            enter_safe_mode(reason);
            log_stability_error(L"E_SUBSYSTEM_CIRCUIT_OPEN",
                                subsystem_to_name(subsystem),
                                operation ? operation : L"UnknownOperation",
                                detail.empty() ? reason : detail,
                                Seh::GetLastSehCode());
        }
    }

    auto JSMod::enter_safe_mode(const std::wstring& reason) -> void
    {
        const bool was_safe_mode = m_safe_mode.exchange(true);
        if (was_safe_mode)
        {
            return;
        }

        Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] SAFE MODE ENABLED: {}\n"), reason);
    }

    auto JSMod::begin_exec_budget() -> void
    {
        set_exec_budget_ms(m_tick_time_budget_ms);
    }

    auto JSMod::set_exec_budget_ms(double budget_ms) -> void
    {
        const int64_t now_us = static_cast<int64_t>(get_current_time() * 1000000.0);
        int64_t budget_us = static_cast<int64_t>(budget_ms * 1000.0);
        if (budget_us <= 0)
        {
            budget_us = 1000;
        }
        m_exec_budget_deadline_us.store(now_us + budget_us);
        m_exec_budget_active.store(true);
    }

    auto JSMod::end_exec_budget() -> void
    {
        m_exec_budget_active.store(false);
        m_exec_budget_deadline_us.store(0);
    }

    auto JSMod::capture_exec_budget() const -> ExecBudgetSnapshot
    {
        return ExecBudgetSnapshot{
            m_exec_budget_active.load(),
            m_exec_budget_deadline_us.load()
        };
    }

    auto JSMod::restore_exec_budget(const ExecBudgetSnapshot& snapshot) -> void
    {
        m_exec_budget_deadline_us.store(snapshot.deadline_us);
        m_exec_budget_active.store(snapshot.active);
    }

    auto JSMod::should_interrupt_execution() const -> bool
    {
        if (!m_exec_budget_active.load())
        {
            return false;
        }

        const int64_t deadline_us = m_exec_budget_deadline_us.load();
        if (deadline_us <= 0)
        {
            return false;
        }

        const int64_t now_us = static_cast<int64_t>(get_current_time() * 1000000.0);
        return now_us >= deadline_us;
    }

    auto JSMod::note_unhandled_promise_rejection() -> void
    {
        m_unhandled_promise_rejections.fetch_add(1);
    }

    // ============================================
    // Initialization
    // ============================================

    auto JSMod::init_runtime() -> bool
    {
        m_runtime = JS_NewRuntime();
        if (!m_runtime)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Failed to create QuickJS runtime\n"));
            return false;
        }

        JS_SetMemoryLimit(m_runtime, 100 * 1024 * 1024);
        JS_SetMaxStackSize(m_runtime, 8 * 1024 * 1024);
        JS_SetGCThreshold(m_runtime, 1024 * 1024);
        JS_SetRuntimeOpaque(m_runtime, this);
        JS_SetInterruptHandler(m_runtime, js_runtime_interrupt_handler, this);
        JS_SetHostPromiseRejectionTracker(m_runtime, js_host_promise_rejection_tracker, this);
        JS_SetCanBlock(m_runtime, false);

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] QuickJS runtime created\n"));
        return true;
    }

    auto JSMod::init_context() -> bool
    {
        m_main_ctx = JS_NewContext(m_runtime);
        if (!m_main_ctx)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Failed to create QuickJS context\n"));
            return false;
        }

        js_std_add_helpers(m_main_ctx, 0, nullptr);
        setup_global_functions(m_main_ctx);
        setup_classes(m_main_ctx);

        m_fetch_worker_stop.store(false);
        m_fetch_worker = std::thread(fetch_worker_run, this);
        m_download_worker_stop.store(false);
        m_download_worker = std::thread(download_worker_run, this);
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] QuickJS context created, fetch/download workers started\n"));
        return true;
    }

    auto JSMod::setup_module_loader() -> void
    {
        JS_SetModuleLoaderFunc(m_runtime, js_module_normalize, js_module_loader, this);
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Module loader configured\n"));
    }

    auto JSMod::setup_global_functions(JSContext* ctx) -> void
    {
        JSValue global = JS_GetGlobalObject(ctx);

        size_t function_index{};
        const size_t api_count = ue4ssl_js_global_api_count();
        for (size_t api_index = 0; api_index < api_count; ++api_index)
        {
            if (!ue4ssl_js_global_api_is_function(api_index))
            {
                continue;
            }

            const char* name = ue4ssl_js_global_api_name(api_index);
            const int arity = ue4ssl_js_global_api_arity(api_index);
            if (!name)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.JavaScript] Skipping global API entry {} with null name\n"), api_index);
                continue;
            }
            if (function_index >= GlobalFunctionImplementations.size())
            {
                Output::send<LogLevel::Error>(
                        STR("[UE4SSL.JavaScript] Rust global API manifest has more functions than the C++ implementation table\n"));
                break;
            }

            JS_SetPropertyStr(
                    ctx,
                    global,
                    name,
                    JS_NewCFunction(ctx, GlobalFunctionImplementations[function_index], name, arity));
            ++function_index;
        }

        if (function_index != GlobalFunctionImplementations.size())
        {
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.JavaScript] Registered {} global functions, expected {}\n"),
                    function_index,
                    GlobalFunctionImplementations.size());
        }

        JSValue ue4ss = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, ue4ss, "version", JS_NewString(ctx, "1.0.0"));
        JS_SetPropertyStr(ctx, ue4ss, "configuration", JS_NewString(ctx, UE4SS_CONFIGURATION));
        const char* ue4ss_name = api_count > 0 ? ue4ssl_js_global_api_name(api_count - 1) : nullptr;
        JS_SetPropertyStr(ctx, global, ue4ss_name ? ue4ss_name : "UE4SS", ue4ss);
        JS_SetPropertyStr(
                ctx,
                global,
                "TextDecoder",
                JS_NewCFunction2(ctx, js_text_decoder_constructor, "TextDecoder", 1, JS_CFUNC_constructor_or_func, 0));

        JS_FreeValue(ctx, global);

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Global functions registered\n"));
    }

    auto JSMod::setup_classes(JSContext* ctx) -> void
    {
        JSUObject::init_class(ctx);
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] UE4 class bindings initialized\n"));
    }

    // ============================================
    // Script Discovery & Execution
    // ============================================

    auto JSMod::find_scripts() -> std::vector<std::filesystem::path>
    {
        std::vector<std::filesystem::path> scripts;

        if (!std::filesystem::exists(m_mods_directory))
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Mods directory does not exist: {}\n"),
                m_mods_directory.wstring());
            return scripts;
        }

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(m_mods_directory, ec))
        {
            if (!entry.is_directory()) continue;

            auto js_script = entry.path() / "js" / "main.js";
            if (std::filesystem::exists(js_script))
            {
                Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Found script: {}\n"), js_script.wstring());
                scripts.push_back(js_script);
            }
        }

        if (ec)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Error scanning mods directory: {}\n"),
                std::wstring(ec.message().begin(), ec.message().end()));
        }

        return scripts;
    }

    auto JSMod::load_and_execute_script(const std::filesystem::path& script_path) -> bool
    {
        if (!m_main_ctx)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Context not initialized\n"));
            return false;
        }

        std::ifstream file(script_path, std::ios::binary);
        if (!file.is_open())
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Failed to open script: {}\n"),
                script_path.wstring());
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();

        std::string filepath = wide_to_utf8(script_path.wstring());
        JSValue result = JS_UNDEFINED;
        if (!safe_js_eval(m_main_ctx, content.c_str(), content.size(),
            filepath.c_str(), JS_EVAL_TYPE_MODULE, &result))
        {
            log_stability_error(L"E_SCRIPT_EVAL_SEH", L"Runtime", L"LoadAndExecuteScript",
                L"JS_Eval SEH exception while loading script", Seh::GetLastSehCode(), 0, script_path.filename().wstring());
            return false;
        }

        if (JS_IsException(result))
        {
            log_exception(m_main_ctx, L"LoadAndExecuteScript", script_path.filename().wstring());
            JS_FreeValue(m_main_ctx, result);
            return false;
        }

        JS_FreeValue(m_main_ctx, result);
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Script executed successfully: {}\n"),
            script_path.filename().wstring());
        return true;
    }

    auto JSMod::execute_string(const std::string& code, const std::string& filename) -> bool
    {
        if (!m_main_ctx)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Context not initialized\n"));
            return false;
        }

        if (!is_event_loop_thread())
        {
            log_stability_error(L"E_THREAD_AFFINITY", L"Runtime", L"ExecuteString",
                L"execute_string must run on the event loop thread");
            return false;
        }

        JSValue result = JS_UNDEFINED;
        if (!safe_js_eval(m_main_ctx, code.c_str(), code.size(),
            filename.c_str(), JS_EVAL_TYPE_GLOBAL, &result))
        {
            log_stability_error(L"E_EXECUTE_STRING_SEH", L"Runtime", L"ExecuteString",
                L"JS_Eval SEH exception in execute_string", Seh::GetLastSehCode(), 0,
                utf8_to_wide(filename));
            return false;
        }

        if (JS_IsException(result))
        {
            log_exception(m_main_ctx, L"ExecuteString", utf8_to_wide(filename));
            JS_FreeValue(m_main_ctx, result);
            return false;
        }

        JS_FreeValue(m_main_ctx, result);
        return true;
    }

    // ============================================
    // Error Handling
    // ============================================

    auto JSMod::log_exception(JSContext* ctx, const wchar_t* operation, const std::wstring& detail) -> bool
    {
        if (!ctx)
        {
            return false;
        }

        const ExecBudgetSnapshot budget_snapshot = capture_exec_budget();
        end_exec_budget();

        JSValue exception = JS_GetException(ctx);
        std::string message_utf8 = js_value_to_utf8_string(ctx, exception);
        if (message_utf8.empty())
        {
            message_utf8 = "(unknown exception)";
        }

        std::string stack_utf8;
        std::string script_utf8 = js_current_script_name_utf8(ctx);
        std::wstring operation_w = operation ? std::wstring(operation) : std::wstring(L"JSException");
        const bool budget_interrupt = message_utf8.find("InternalError: interrupted") != std::string::npos
            || message_utf8 == "interrupted";

        if (budget_interrupt)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Budget interrupt [{}]: {}\n"),
                operation_w,
                utf8_to_wide(message_utf8));
        }
        else
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Exception [{}]: {}\n"),
                operation_w,
                utf8_to_wide(message_utf8));
        }
        if (!detail.empty())
        {
            if (budget_interrupt)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Detail: {}\n"), detail);
            }
            else
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Detail: {}\n"), detail);
            }
        }

        JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
        if (!JS_IsUndefined(stack))
        {
            stack_utf8 = js_value_to_utf8_string(ctx, stack);
            if (!stack_utf8.empty())
            {
                if (budget_interrupt)
                {
                    Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Stack: {}\n"),
                        utf8_to_wide(stack_utf8));
                }
                else
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Stack: {}\n"),
                        utf8_to_wide(stack_utf8));
                }
            }
        }
        if (!script_utf8.empty())
        {
            if (budget_interrupt)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Script: {}\n"),
                    utf8_to_wide(script_utf8));
            }
            else
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Script: {}\n"),
                    utf8_to_wide(script_utf8));
            }
        }

        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, exception);
        restore_exec_budget(budget_snapshot);
        return budget_interrupt;
    }

} // namespace RC::JSScript
