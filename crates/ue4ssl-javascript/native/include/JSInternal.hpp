#pragma once

#include "JSMod.hpp"
#include "JSType/JSUObject.hpp"

#include <cstdint>
#include <string>

#define NOMINMAX
#include <Windows.h>

#include <SehFramework.hpp>

// QuickJS headers (suppress C4244 from third-party header)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#endif
extern "C" {
#include "quickjs.h"
#include "quickjs-libc.h"
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace RC::JSScript
{
    // ============================================
    // Shared utilities
    // ============================================

    JSMod* get_js_mod(JSContext* ctx);
    std::wstring utf8_to_wide(const std::string& utf8);
    std::string  wide_to_utf8(const std::wstring& wide);

    // Delegates to Seh::LogStabilityError from the unified framework
    inline void log_stability_error(const wchar_t* error_code,
                                    const wchar_t* subsystem,
                                    const wchar_t* operation,
                                    const std::wstring& message,
                                    uint32_t seh_code = 0,
                                    uintptr_t object_addr = 0,
                                    const std::wstring& script_name = L"-")
    {
        Seh::LogStabilityError(error_code, subsystem, operation, message, seh_code, object_addr, script_name);
    }

    // ============================================
    // Property conversion (JSPropertyUtils.cpp)
    // ============================================

    JSValue property_to_jsvalue(JSContext* ctx, Unreal::FProperty* prop, void* data);
    void    jsvalue_to_property(JSContext* ctx, Unreal::FProperty* prop, void* data, JSValue val);
    JSValue create_param_ref(JSContext* ctx, Unreal::FProperty* prop, void* data);
    void    ensure_param_ref_class(JSContext* ctx);
    void    reset_param_ref_class(JSContext* ctx);

    void    reset_delegate_statics();

    // ============================================
    // SEH helpers - QuickJS specific (remain here)
    // ============================================

    // Delegates to Seh::GetLastSehCode()
    inline DWORD get_last_seh_code() { return Seh::GetLastSehCode(); }

    bool safe_js_call(JSContext* ctx, JSValueConst func, JSValueConst this_obj,
                      int argc, JSValueConst* argv, JSValue* out_result);
    bool safe_js_eval(JSContext* ctx, const char* input, size_t input_len,
                      const char* filename, int eval_flags, JSValue* out_result);
    int  safe_js_execute_pending_job(JSRuntime* rt, JSContext** pctx);

    // Delegates to unified framework
    inline bool safe_process_event(Unreal::UObject* object, Unreal::UFunction* function, void* params) {
        return Seh::SafeProcessEvent(object, function, params);
    }
    bool safe_has_iterable_properties(Unreal::UFunction* func);

    // ============================================
    // JS binding functions - Global functions (JSGlobalFunctions.cpp)
    // ============================================

    JSValue js_print(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_find_first_of(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_find_all_of(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_find_all_actors_with_interface(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_static_find_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_load_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_scan_blueprint_widgets_by_interface(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_register_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_register_bind_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_register_native_object_method_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_register_load_map_pre_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_register_load_map_post_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_register_pe_watch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_hook_ufunction(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_unregister_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_unregister_bind_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_unregister_load_map_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_notify_on_new_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_register_key_bind(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_call_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_call_function_ex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_with_exec_budget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // ============================================
    // JS binding functions - Timers (JSTimer.cpp)
    // ============================================

    JSValue js_set_timeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_set_interval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_clear_timeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_clear_interval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // ============================================
    // JS binding functions - Fetch (JSFetch.cpp)
    // ============================================

    void    fetch_worker_run(JSMod* mod);
    void    download_worker_run(JSMod* mod);
    JSValue js_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_fetch_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_fetch_body_get_reader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_fetch_stream_reader_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_text_decoder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    JSValue js_text_decoder_decode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // ============================================
    // JS binding functions - Memory (JSMemory.cpp)
    // ============================================

    JSValue js_sig_scan(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_patch_byte(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_read_byte(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // ============================================
    // JS binding functions - File I/O (JSFileIO.cpp)
    // ============================================

    JSValue js_read_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_write_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_get_mods_directory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_get_game_directory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_download_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_download_file_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_play_sound_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_stop_sound(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // ============================================
    // JS binding functions - Property Access (JSPropertyAccess.cpp)
    // ============================================

    JSValue js_get_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_set_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_export_property_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_get_property_path(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_set_property_path(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_apply_object_patch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // ============================================
    // JS binding functions - Delegate (JSDelegate.cpp)
    // ============================================

    JSValue js_bind_delegate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_unbind_delegate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_clear_delegate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_bind_delegate_callback(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_unbind_delegate_callback(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // ============================================
    // JS binding functions - UMG / NewObject (JSGameThreadDispatcher.cpp)
    // ============================================

    JSValue js_new_uobject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_umg_dispatch_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_umg_dispatch_async(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_umg_create_user_widget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_umg_clone_user_widget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_umg_set_user_widget_root(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    JSValue js_umg_construct_widget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // ============================================
    // Module loader (JSPropertyUtils.cpp)
    // ============================================

    char*        js_module_normalize(JSContext* ctx, const char* base_name, const char* name, void* opaque);
    JSModuleDef* js_module_loader(JSContext* ctx, const char* module_name, void* opaque);

} // namespace RC::JSScript
