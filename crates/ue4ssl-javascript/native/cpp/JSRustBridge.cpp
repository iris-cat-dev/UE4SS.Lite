#include <cstddef>
#include <string>

#define NOMINMAX
#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>

#include "JSMod.hpp"

using namespace RC;

extern "C"
{
    void* ue4ssl_js_cpp_create()
    {
        try
        {
            return new JSScript::JSMod();
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Failed to create JSMod bridge instance\n"));
            return nullptr;
        }
    }

    void ue4ssl_js_cpp_destroy(void* engine)
    {
        if (!engine)
        {
            return;
        }

        try
        {
            delete static_cast<JSScript::JSMod*>(engine);
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Exception while destroying JSMod bridge instance\n"));
        }
    }

    int ue4ssl_js_cpp_init_engine(void* engine)
    {
        if (!engine)
        {
            return 0;
        }

        __try
        {
            return static_cast<JSScript::JSMod*>(engine)->init_engine() ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    int ue4ssl_js_cpp_load_scripts(void* engine)
    {
        if (!engine)
        {
            return 0;
        }

        __try
        {
            return static_cast<JSScript::JSMod*>(engine)->load_scripts() ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    int ue4ssl_js_cpp_tick(void* engine)
    {
        if (!engine)
        {
            return 0;
        }

        __try
        {
            static_cast<JSScript::JSMod*>(engine)->tick();
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool ue4ssl_js_cpp_is_initialized(void* engine)
    {
        if (!engine)
        {
            return false;
        }

        try
        {
            return static_cast<JSScript::JSMod*>(engine)->is_initialized();
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] is_initialized bridge exception\n"));
            return false;
        }
    }

    bool ue4ssl_js_cpp_eval(void* engine, const char* code, size_t code_len, const char* filename)
    {
        if (!engine || !code)
        {
            return false;
        }

        try
        {
            return static_cast<JSScript::JSMod*>(engine)->execute_string(
                std::string(code, code_len),
                filename ? filename : "<eval>");
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] eval bridge exception\n"));
            return false;
        }
    }
}
