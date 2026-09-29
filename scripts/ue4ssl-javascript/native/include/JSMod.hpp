#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <future>
#include <functional>

#include "JSGameThreadDispatcher.hpp"
#include <Unreal/Hooks/GlobalCallbackId.hpp>

// QuickJS headers
extern "C" {
#include "quickjs.h"
}

// Forward declarations for Unreal types
namespace RC::Unreal
{
    class FProperty;
    class UFunction;
    class UObject;
    class UnrealScriptFunctionCallableContext;
    using CallbackId = int32_t;
}

namespace RC::JSScript
{
    class JSMod;  // Forward declaration

    /**
     * JSMod - JavaScript scripting engine manager based on QuickJS
     * 
     * This class manages the QuickJS runtime and context, handles script loading
     * and execution, and provides the bridge between JavaScript and UE4.
     */
    class JSMod
    {
    public:
        enum class GuardedSubsystem : uint8_t
        {
            Hook = 0,
            Timer,
            Fetch,
            Delegate,
            Count
        };

        struct SubsystemCircuitState
        {
            uint32_t consecutive_failures{0};
            bool disabled{false};
        };

        // JavaScript UFunction Hook data
        struct JSUFunctionHookData
        {
            JSMod* owner;                      // Pointer to JSMod instance
            JSContext* ctx;                    // JS context
            JSValue pre_callback;              // JS pre-hook callback function
            JSValue post_callback;             // JS post-hook callback function
            Unreal::UFunction* function;       // The hooked UFunction
            Unreal::CallbackId pre_id;         // Pre-hook callback ID
            Unreal::CallbackId post_id;        // Post-hook callback ID
            bool has_return_value;             // Does the function have a return value
            bool is_executing{false};          // Recursion guard - prevents re-entry during hook execution
            bool force_sync{false};             // Execute callback in the UE hook callstack instead of deferring to JS tick.
        };

        struct JSBindHookData
        {
            JSMod* owner{nullptr};
            JSContext* ctx{nullptr};
            JSValue pre_callback{JS_UNDEFINED};
            JSValue post_callback{JS_UNDEFINED};
            std::wstring function_path{};
            int32_t bind_id{0};
            Unreal::UFunction* current_function{nullptr};
            Unreal::CallbackId active_pre_id{0};
            Unreal::CallbackId active_post_id{0};
        };

        struct NativeMethodArg
        {
            enum class Type : uint8_t { Null, Bool, Int, Float, String };
            Type type{Type::Null};
            bool bool_value{false};
            int64_t int_value{0};
            double float_value{0.0};
            std::wstring string_value{};
        };

        struct NativeObjectMethodHookData
        {
            JSMod* owner{nullptr};
            Unreal::UFunction* hook_function{nullptr};
            Unreal::CallbackId pre_id{0};
            Unreal::CallbackId post_id{0};
            bool run_pre{false};
            bool run_post{true};
            bool target_context{true};
            int32_t target_param_index{0};
            std::wstring target_filter{};
            std::wstring method_name{};
            std::wstring diagnostic_label{};
            std::vector<NativeMethodArg> args{};
            bool diagnose_params{false};
            uint32_t execution_log_count{0};
        };

        // Legacy callback data for hooks (kept for compatibility)
        struct HookCallback
        {
            JSContext* ctx;
            JSValue callback;  // Reference to JS callback function
            int32_t ref_id;    // Registry reference ID
        };
        
        // Timer data for setTimeout/setInterval
        struct TimerCallback
        {
            JSContext* ctx;
            JSValue callback;       // Reference to JS callback function
            int32_t id;             // Timer ID
            double trigger_time;    // When to trigger (in seconds since start)
            double interval;        // Interval for setInterval (0 for setTimeout)
            bool is_interval;       // true = setInterval, false = setTimeout
            bool cancelled;         // Timer was cancelled
            bool is_executing{false}; // Recursion guard - prevents re-entry during callback execution
        };

        // Key binding callback data
        struct KeyBindCallback
        {
            JSMod* owner;           // Pointer to JSMod instance
            JSContext* ctx;         // JS context
            JSValue callback;       // JS callback function
            uint8_t key;            // Key code
            bool with_ctrl;         // Requires CTRL
            bool with_shift;        // Requires SHIFT
            bool with_alt;          // Requires ALT
            bool is_executing{false}; // Recursion guard - prevents re-entry during callback execution
        };

        // Pending game thread ProcessEvent call (for RPC/net functions)
        struct PendingGameThreadCall
        {
            Unreal::UObject* object;
            Unreal::UFunction* function;
            void* params_memory;
            std::vector<wchar_t*> raw_string_buffers;
        };

        // A single hook parameter value extracted on the game thread (as C++ types, not JSValue)
        struct PendingHookCallbackParam
        {
            enum class Type { String, StringArray, Int, Float, Bool, Object, Json, Unknown };
            Type type{Type::Unknown};
            std::wstring str_val{};
            std::vector<std::wstring> str_array_val{};
            int64_t int_val{0};
            double float_val{0.0};
            bool bool_val{false};
            Unreal::UObject* obj_val{nullptr};
            bool is_out_param{false};
            Unreal::FProperty* prop_ref{nullptr};
            void* data_ref{nullptr};
        };

        // A hook callback queued from the game thread for deferred execution on the event loop thread
        struct PendingHookCallback
        {
            JSUFunctionHookData* hook_data;       // Pointer to hook data (owns JS callback refs)
            bool is_pre;                           // true = pre-callback, false = post-callback
            Unreal::UObject* context_object;       // 'this' UObject
            std::vector<PendingHookCallbackParam> params;
        };

        struct PendingBindHookActivation
        {
            std::wstring function_path{};
        };

        struct LoadMapCallbackRegistration
        {
            int32_t id{0};
            JSContext* ctx{nullptr};
            JSValue callback{JS_UNDEFINED};
            bool is_pre{false};
        };

        struct PendingLoadMapEvent
        {
            bool is_pre{false};
            bool has_pending_net_game{false};
            std::wstring map{};
            std::wstring host{};
            std::wstring portal{};
            std::wstring redirect_url{};
            std::wstring error{};
        };

        struct ExecBudgetSnapshot
        {
            bool active{false};
            int64_t deadline_us{0};
        };

    public:
        // UFunction bind-hook management
        std::vector<std::unique_ptr<JSBindHookData>> m_bind_hooks;
        std::mutex m_bind_hooks_mutex;
        std::vector<PendingBindHookActivation> m_pending_bind_hook_activations;
        std::mutex m_pending_bind_hook_activations_mutex;
        int32_t m_next_bind_hook_id{1};
        Unreal::Hook::GlobalCallbackId m_bind_watch_callback_id{Unreal::Hook::ERROR_ID};

        // LoadMap callback bridge (game thread -> JS event loop)
        std::vector<LoadMapCallbackRegistration> m_load_map_callbacks;
        std::mutex m_load_map_callbacks_mutex;
        std::vector<PendingLoadMapEvent> m_pending_load_map_events;
        std::mutex m_pending_load_map_events_mutex;
        int32_t m_next_load_map_callback_id{1};
        Unreal::Hook::GlobalCallbackId m_load_map_pre_callback_id{Unreal::Hook::ERROR_ID};
        Unreal::Hook::GlobalCallbackId m_load_map_post_callback_id{Unreal::Hook::ERROR_ID};

        // UFunction hook management (public for access from global functions)
        std::vector<std::unique_ptr<JSUFunctionHookData>> m_ufunction_hooks;
        std::mutex m_ufunction_hooks_mutex;
        std::vector<std::unique_ptr<NativeObjectMethodHookData>> m_native_method_hooks;
        std::mutex m_native_method_hooks_mutex;

        // Legacy hook management (public for access from global functions)
        std::vector<HookCallback> m_hook_callbacks;
        std::mutex m_hooks_mutex;
        
        // Timer management (public for access from global functions)
        std::vector<TimerCallback> m_timers;
        std::mutex m_timers_mutex;
        int32_t m_next_timer_id{1};
        double m_start_time{0.0};
        
        // Key binding management (public for access from global functions)
        std::vector<std::unique_ptr<KeyBindCallback>> m_key_bindings;
        std::mutex m_key_bindings_mutex;
        
        // Pending keybind callbacks to execute on main thread (thread-safety fix)
        std::vector<KeyBindCallback*> m_pending_keybind_callbacks;
        std::mutex m_pending_keybind_mutex;

        // Game thread call queue (for RPC-enabled net functions)
        std::vector<PendingGameThreadCall> m_pending_game_thread_calls;
        std::mutex m_pending_game_thread_mutex;
        std::thread::id m_event_loop_thread_id{};
        bool m_game_thread_callback_registered{false};

        // ProcessEvent watches - intercept specific functions via ProcessEvent global hook
        struct ProcessEventWatch
        {
            std::wstring function_name;
            Unreal::UFunction* cached_func{nullptr};
            JSValue callback{JS_UNDEFINED};
        };
        std::vector<ProcessEventWatch> m_pe_watches;
        std::mutex m_pe_watches_mutex;
        bool m_pe_watch_hook_registered{false};

        // Pending hook callbacks from game thread (deferred to event loop thread)
        std::vector<PendingHookCallback> m_pending_hook_callbacks;
        std::mutex m_pending_hook_callbacks_mutex;

        // Fetch: pending requests (id -> resolve/reject), request queue for worker, result queue for main
        struct PendingFetchCallbacks
        {
            JSValue resolve_func;
            JSValue reject_func;
        };
        struct FetchRequest
        {
            int64_t id{0};
            std::string url{};
            std::string method{};
            std::string headers_str{};
            std::string body{};
            bool stream_response{false};
        };
        struct FetchStreamState
        {
            int status{0};
            bool closed{false};
            std::string body{};
            std::string error_msg{};
            std::vector<std::string> chunks{};
            std::vector<PendingFetchCallbacks> pending_reads{};
        };
        std::unordered_map<int64_t, PendingFetchCallbacks> m_fetch_pending;
        std::unordered_map<int64_t, FetchStreamState> m_fetch_streams;
        std::vector<FetchRequest> m_fetch_request_queue;
        struct CompletedFetchResult
        {
            enum class Kind : uint8_t
            {
                Complete = 0,
                StreamHeaders,
                StreamChunk,
                StreamEnd,
                StreamError
            };
            int64_t id;
            Kind kind{Kind::Complete};
            int status{0};
            std::string body;
            std::string error_msg;
        };
        std::vector<CompletedFetchResult> m_fetch_result_queue;
        std::mutex m_fetch_mutex;
        std::condition_variable m_fetch_request_cv;
        int64_t m_fetch_next_id{1};
        std::thread m_fetch_worker;
        std::atomic<bool> m_fetch_worker_stop{false};

        struct PendingDownloadCallbacks
        {
            JSValue resolve_func;
            JSValue reject_func;
        };
        struct DownloadRequest
        {
            int64_t id{0};
            std::string url{};
            std::string output_path{};
            int timeout_ms{30000};
            std::string headers_str{};
        };
        struct CompletedDownloadResult
        {
            int64_t id{0};
            bool ok{false};
            int status{0};
            int64_t bytes_written{0};
            std::string path{};
            std::string error_msg{};
        };
        std::unordered_map<int64_t, PendingDownloadCallbacks> m_download_pending;
        std::vector<DownloadRequest> m_download_request_queue;
        std::vector<CompletedDownloadResult> m_download_result_queue;
        std::mutex m_download_mutex;
        std::condition_variable m_download_request_cv;
        int64_t m_download_next_id{1};
        std::thread m_download_worker;
        std::atomic<bool> m_download_worker_stop{false};
        
        // Module cache (public for access from module loader)
        std::unordered_map<std::string, bool> m_loaded_modules;

        // Mutex protecting QuickJS context access (for cross-thread JS execution)
        std::recursive_mutex m_js_mutex;

        // General-purpose game thread dispatcher (for UMG and other operations)
        GameThreadDispatcher m_umg_dispatcher;
        bool m_umg_dispatcher_registered{false};
        auto setup_umg_dispatcher() -> void;

    private:
        std::filesystem::path m_mods_directory;
        JSRuntime* m_runtime{nullptr};
        JSContext* m_main_ctx{nullptr};
        
        bool m_initialized{false};
        bool m_in_tick{false};  // Recursion guard for tick()
        uint32_t m_tick_seh_failures{0};

        // Thread affinity and execution budget
        std::atomic<bool> m_exec_budget_active{false};
        std::atomic<int64_t> m_exec_budget_deadline_us{0};
        uint32_t m_tick_job_budget{256};
        double m_tick_time_budget_ms{6.0};

        // Promise rejection telemetry
        std::atomic<uint64_t> m_unhandled_promise_rejections{0};

        // Subsystem circuit breakers and safe mode
        std::array<SubsystemCircuitState, static_cast<size_t>(GuardedSubsystem::Count)> m_subsystem_circuit_states{};
        std::mutex m_subsystem_circuit_mutex;
        uint32_t m_subsystem_failure_threshold{5};
        std::atomic<bool> m_safe_mode{false};

    public:
        JSMod();
        ~JSMod();

        // Lifecycle
        auto start() -> bool;           // Full start (init + load scripts) - for backward compatibility
        auto init_engine() -> bool;     // Initialize engine only (safe before UE init)
        auto load_scripts() -> bool;    // Load and execute scripts (call after UE init). Returns true if scripts were found.
        auto stop() -> void;
        auto tick() -> void;  // Called every frame
        auto tick_impl() -> void; // Internal tick body invoked by SEH wrapper
        
        // Script execution
        auto load_and_execute_script(const std::filesystem::path& script_path) -> bool;
        auto execute_string(const std::string& code, const std::string& filename = "<eval>") -> bool;
        
        // Timer management
        auto add_timer(JSContext* ctx, JSValue callback, double delay_ms, bool is_interval) -> int32_t;
        auto cancel_timer(int32_t id) -> bool;
        auto process_timers() -> void;
        
        // UFunction hook management
        auto register_ufunction_hook(JSContext* ctx, Unreal::UFunction* function,
                                     JSValue pre_callback, JSValue post_callback, bool force_sync = false) -> std::pair<int32_t, int32_t>;
        auto unregister_ufunction_hook(Unreal::CallbackId pre_id, Unreal::CallbackId post_id) -> bool;
        auto register_native_object_method_hook(std::unique_ptr<NativeObjectMethodHookData> hook_data) -> std::pair<int32_t, int32_t>;
        auto register_bind_hook(JSContext* ctx, const std::wstring& function_path,
                                JSValue pre_callback, JSValue post_callback) -> std::pair<int32_t, int32_t>;
        auto unregister_bind_hook(int32_t first_id, int32_t second_id) -> bool;
        auto process_pending_bind_hook_activations() -> void;
        auto clear_bind_hook_state() -> void;
        auto register_load_map_hook(JSContext* ctx, bool is_pre, JSValue callback) -> int32_t;
        auto unregister_load_map_hook(int32_t id) -> bool;
        auto process_pending_load_map_events() -> void;
        auto clear_load_map_hook_state() -> void;
        auto ensure_load_map_callbacks_registered() -> bool;

        // Key binding management
        auto register_key_bind(JSContext* ctx, uint8_t key, JSValue callback, 
                              bool with_ctrl, bool with_shift, bool with_alt) -> bool;

        // Game thread dispatcher for RPC calls
        auto setup_game_thread_dispatcher() -> void;

        // ProcessEvent-level function watch (safe - does not modify FuncPtrs)
        auto register_process_event_watch(JSContext* ctx, const std::wstring& func_name, JSValue callback) -> int;
        auto setup_pe_watch_hook() -> void;

        // Static hook callbacks for UE4SS hook system
        static void js_ufunction_hook_pre(Unreal::UnrealScriptFunctionCallableContext& context, void* custom_data);
        static void js_ufunction_hook_post(Unreal::UnrealScriptFunctionCallableContext& context, void* custom_data);
        static void native_object_method_hook_pre(Unreal::UnrealScriptFunctionCallableContext& context, void* custom_data);
        static void native_object_method_hook_post(Unreal::UnrealScriptFunctionCallableContext& context, void* custom_data);
        
        // Getters
        [[nodiscard]] auto get_runtime() const -> JSRuntime* { return m_runtime; }
        [[nodiscard]] auto get_main_context() const -> JSContext* { return m_main_ctx; }
        [[nodiscard]] auto get_mods_directory() const -> const std::filesystem::path& { return m_mods_directory; }
        [[nodiscard]] auto is_initialized() const -> bool { return m_initialized; }
        [[nodiscard]] auto get_current_time() const -> double;
        [[nodiscard]] auto is_event_loop_thread() const -> bool { return std::this_thread::get_id() == m_event_loop_thread_id; }
        [[nodiscard]] auto is_safe_mode() const -> bool { return m_safe_mode.load(); }
        [[nodiscard]] auto is_subsystem_disabled(GuardedSubsystem subsystem) -> bool;
        auto report_subsystem_success(GuardedSubsystem subsystem) -> void;
        auto report_subsystem_failure(GuardedSubsystem subsystem, const wchar_t* operation, const std::wstring& detail = L"") -> void;
        auto enter_safe_mode(const std::wstring& reason) -> void;
        auto begin_exec_budget() -> void;
        auto set_exec_budget_ms(double budget_ms) -> void;
        auto end_exec_budget() -> void;
        [[nodiscard]] auto capture_exec_budget() const -> ExecBudgetSnapshot;
        auto restore_exec_budget(const ExecBudgetSnapshot& snapshot) -> void;
        [[nodiscard]] auto should_interrupt_execution() const -> bool;
        auto note_unhandled_promise_rejection() -> void;
        [[nodiscard]] auto get_unhandled_promise_rejections() const -> uint64_t { return m_unhandled_promise_rejections.load(); }
        auto log_exception(JSContext* ctx,
                           const wchar_t* operation = L"JSException",
                           const std::wstring& detail = L"") -> bool;

    private:
        // Initialization
        auto init_runtime() -> bool;
        auto init_context() -> bool;
        auto setup_global_functions(JSContext* ctx) -> void;
        auto setup_classes(JSContext* ctx) -> void;
        auto setup_module_loader() -> void;
        
        // Script discovery
        auto find_scripts() -> std::vector<std::filesystem::path>;

        // Fetch: process completed HTTP results on event loop thread
        auto process_fetch_results() -> void;
        auto process_download_results() -> void;
    };

} // namespace RC::JSScript
