#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <string>

#define NOMINMAX
#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>

using namespace RC;

struct UE4SSLJsEngine;

extern "C"
{
    UE4SSLJsEngine* ue4ssl_js_create();
    void ue4ssl_js_destroy(UE4SSLJsEngine* engine);
    int ue4ssl_js_init_engine(UE4SSLJsEngine* engine);
    int ue4ssl_js_load_scripts(UE4SSLJsEngine* engine);
    int ue4ssl_js_tick(UE4SSLJsEngine* engine);
    bool ue4ssl_js_is_initialized(const UE4SSLJsEngine* engine);
    bool ue4ssl_js_eval(UE4SSLJsEngine* engine, const char* code, size_t code_len, const char* filename);
}

static double now_ms()
{
    using C = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(C::now().time_since_epoch()).count();
}

/**
 * JSScriptMod - C++ mod entry point for JavaScript scripting support
 * 
 * This mod provides JavaScript scripting capabilities to UE4SS using QuickJS,
 * similar to the built-in Lua scripting support.
 */
class JSScriptMod : public CppUserModBase
{
private:
    UE4SSLJsEngine* m_js_engine{nullptr};
    std::atomic<bool> m_unreal_ready{false}; // Set on init thread, read on event loop thread
    bool m_engine_initialized = false;       // Only accessed on event loop thread
    bool m_scripts_loaded = false;           // Only accessed on event loop thread
    uint64_t m_update_call_count{0};
    double m_last_update_ms{0.0};

public:
    JSScriptMod() : CppUserModBase()
    {
        ModName = STR("UE4SSL.JavaScript");
        ModVersion = STR("1.0.0");
        ModDescription = STR("JavaScript scripting support via QuickJS engine");
        ModAuthors = STR("UE4SS Community");
        
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Mod loaded, waiting for event loop thread...\n"));
        
        // Create the Rust-owned engine facade but don't initialize yet.
        // All JS operations will happen on the event loop thread for thread safety.
        m_js_engine = ue4ssl_js_create();
        if (!m_js_engine)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Failed to create Rust JS engine facade\n"));
        }
    }

    ~JSScriptMod() override
    {
        try
        {
            if (m_js_engine)
            {
                ue4ssl_js_destroy(m_js_engine);
                m_js_engine = nullptr;
            }
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Exception during shutdown\n"));
        }
    }

    /**
     * Called when UE4 is fully initialized
     * Just set a flag - actual initialization happens on event loop thread
     */
    auto on_unreal_init() -> void override
    {
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] on_unreal_init called - Unreal is ready\n"));
        m_unreal_ready = true;
    }

    /**
     * Called every frame on the event loop thread
     * All JS operations happen here for thread safety
     */
    auto on_update() -> void override
    {
        if (!m_js_engine) return;

        m_update_call_count++;
        const double t0 = now_ms();

        // Initialize engine on first update (event loop thread)
        if (!m_engine_initialized)
        {
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Initializing JS engine on event loop thread... (update#{})\n"), m_update_call_count);
            int init_result = ue4ssl_js_init_engine(m_js_engine);
            if (init_result == 1)
            {
                m_engine_initialized = true;
                Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] JS engine initialized on event loop thread (took {:.1f}ms)\n"), now_ms() - t0);
            }
            else if (init_result == -1)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] JS engine init SEH exception! Engine disabled.\n"));
                ue4ssl_js_destroy(m_js_engine);
                m_js_engine = nullptr;
            }
            else
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Failed to initialize JS engine\n"));
            }
            return;
        }
        
        // Load scripts once UE is ready (still on event loop thread)
        if (m_unreal_ready && !m_scripts_loaded && ue4ssl_js_is_initialized(m_js_engine))
        {
            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Loading scripts on event loop thread... (update#{}, gap_since_init={:.1f}ms)\n"),
                m_update_call_count, t0 - m_last_update_ms);
            int load_result = ue4ssl_js_load_scripts(m_js_engine);
            if (load_result == -1)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] load_scripts SEH exception caught\n"));
            }
            else if (load_result == 0)
            {
                Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] No local JS mods found; engine stays alive for external eval (e.g. UE4SSL.DRG). Dispatchers will be registered lazily on first use.\n"));
            }
            m_scripts_loaded = true;
        }

        // Log slow ticks (> 50ms gap between updates)
        if (m_scripts_loaded && (t0 - m_last_update_ms) > 50.0 && m_last_update_ms > 0.0)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] [PERF] Slow event loop: {:.1f}ms since last update (update#{})\n"),
                t0 - m_last_update_ms, m_update_call_count);
        }

        m_last_update_ms = t0;
        
        // Normal tick with SEH + try-catch
        if (m_js_engine && ue4ssl_js_is_initialized(m_js_engine))
        {
            try
            {
                if (!ue4ssl_js_tick(m_js_engine))
                {
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] tick() SEH exception caught, engine still running\n"));
                }
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] tick() C++ exception: {}\n"),
                    std::wstring(e.what(), e.what() + strlen(e.what())));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] tick() unknown C++ exception\n"));
            }
        }

        double elapsed = now_ms() - t0;
        if (elapsed > 20.0)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] [PERF] Tick took {:.1f}ms (update#{})\n"), elapsed, m_update_call_count);
        }
    }

    /**
     * Called when the program starts (before Unreal init)
     */
    auto on_program_start() -> void override
    {
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] on_program_start called\n"));
    }

    auto get_js_engine() const -> UE4SSLJsEngine* { return m_js_engine; }
};

// Global Rust engine facade pointer for cross-module access.
static UE4SSLJsEngine* g_js_engine_instance = nullptr;

// DLL export functions
#define JS_SCRIPT_MOD_API __declspec(dllexport)

extern "C"
{
    JS_SCRIPT_MOD_API CppUserModBase* start_mod()
    {
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] start_mod called\n"));
        auto* mod = new JSScriptMod();
        g_js_engine_instance = mod->get_js_engine();
        return mod;
    }

    JS_SCRIPT_MOD_API void uninstall_mod(CppUserModBase* mod)
    {
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] uninstall_mod called\n"));
        g_js_engine_instance = nullptr;
        try
        {
            delete mod;
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Exception during uninstall_mod delete\n"));
        }
    }

    /**
     * Cross-module API: evaluate a JS string in the shared QuickJS context.
     * MUST be called from the event loop thread (i.e. from another mod's on_update).
     * Returns true on success, false on error or if engine is not ready.
     */
    JS_SCRIPT_MOD_API bool eval_js_code(const char* code, size_t code_len, const char* filename)
    {
        if (!g_js_engine_instance || !ue4ssl_js_is_initialized(g_js_engine_instance))
            return false;
        try
        {
            return ue4ssl_js_eval(g_js_engine_instance, code, code_len, filename);
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] eval_js_code exception\n"));
            return false;
        }
    }
}
