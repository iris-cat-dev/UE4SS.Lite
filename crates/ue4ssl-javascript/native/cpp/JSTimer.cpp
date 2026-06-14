#include "JSInternal.hpp"

#include <algorithm>

#include <DynamicOutput/DynamicOutput.hpp>

namespace RC::JSScript
{
    // ============================================
    // Timer Management (JSMod member functions)
    // ============================================

    auto JSMod::add_timer(JSContext* ctx, JSValue callback, double delay_ms, bool is_interval) -> int32_t
    {
        std::lock_guard<std::mutex> lock(m_timers_mutex);

        int32_t id = m_next_timer_id++;
        double trigger_time = get_current_time() + (delay_ms / 1000.0);

        TimerCallback timer;
        timer.ctx = ctx;
        timer.callback = JS_DupValue(ctx, callback);
        timer.id = id;
        timer.trigger_time = trigger_time;
        timer.interval = is_interval ? (delay_ms / 1000.0) : 0.0;
        timer.is_interval = is_interval;
        timer.cancelled = false;

        m_timers.push_back(timer);
        return id;
    }

    auto JSMod::cancel_timer(int32_t id) -> bool
    {
        std::lock_guard<std::mutex> lock(m_timers_mutex);

        for (auto& timer : m_timers)
        {
            if (timer.id == id && !timer.cancelled)
            {
                timer.cancelled = true;
                return true;
            }
        }
        return false;
    }

    static bool safe_timer_js_call(JSMod* mod, JSContext* ctx, JSValue callback)
    {
        JSValue result = JS_UNDEFINED;
        if (!safe_js_call(ctx, callback, JS_UNDEFINED, 0, nullptr, &result))
        {
            mod->report_subsystem_failure(JSMod::GuardedSubsystem::Timer, L"TimerCallback", L"SEH exception");
            return false;
        }
        if (JS_IsException(result))
        {
            mod->log_exception(ctx, L"TimerCallback");
            JS_FreeValue(ctx, result);
            mod->report_subsystem_failure(JSMod::GuardedSubsystem::Timer, L"TimerCallback", L"JS exception");
            return false;
        }
        JS_FreeValue(ctx, result);
        mod->report_subsystem_success(JSMod::GuardedSubsystem::Timer);
        return true;
    }

    auto JSMod::process_timers() -> void
    {
        if (!m_main_ctx) return;

        if (is_subsystem_disabled(GuardedSubsystem::Timer))
        {
            std::lock_guard<std::mutex> lock(m_timers_mutex);
            for (auto& timer : m_timers)
            {
                timer.cancelled = true;
            }
        }

        double current_time = get_current_time();

        struct TimerToFire {
            int32_t id;
            JSValue callback;
            bool is_interval;
            TimerCallback* timer_ptr;
        };
        std::vector<TimerToFire> to_fire;

        {
            std::lock_guard<std::mutex> lock(m_timers_mutex);

            for (auto& timer : m_timers)
            {
                if (timer.cancelled) continue;
                if (timer.is_executing) continue;
                if (current_time < timer.trigger_time) continue;

                to_fire.push_back({timer.id, JS_DupValue(timer.ctx, timer.callback), timer.is_interval, &timer});

                timer.is_executing = true;

                if (timer.is_interval)
                {
                    timer.trigger_time = current_time + timer.interval;
                }
                else
                {
                    timer.cancelled = true;
                }
            }
        }

        for (auto& item : to_fire)
        {
            safe_timer_js_call(this, m_main_ctx, item.callback);
            JS_FreeValue(m_main_ctx, item.callback);

            if (item.timer_ptr)
            {
                item.timer_ptr->is_executing = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_timers_mutex);
            for (auto& timer : m_timers)
            {
                if (timer.cancelled && timer.ctx)
                {
                    JS_FreeValue(timer.ctx, timer.callback);
                    timer.ctx = nullptr;
                }
            }
            m_timers.erase(
                std::remove_if(m_timers.begin(), m_timers.end(),
                    [](const TimerCallback& t) { return t.cancelled; }),
                m_timers.end()
            );
        }
    }

    // ============================================
    // JS Timer Functions
    // ============================================

    JSValue js_set_timeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "setTimeout requires 2 arguments: callback, delay");
        if (!JS_IsFunction(ctx, argv[0]))
            return JS_ThrowTypeError(ctx, "First argument must be a callback function");

        double delay_ms;
        if (JS_ToFloat64(ctx, &delay_ms, argv[1]) != 0)
            return JS_ThrowTypeError(ctx, "Second argument must be a number (delay in ms)");
        if (delay_ms < 0) delay_ms = 0;

        JSMod* mod = get_js_mod(ctx);
        if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
        if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Timer))
            return JS_ThrowInternalError(ctx, "Timer subsystem disabled by circuit breaker");

        int32_t timer_id = mod->add_timer(ctx, argv[0], delay_ms, false);
        return JS_NewInt32(ctx, timer_id);
    }

    JSValue js_set_interval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "setInterval requires 2 arguments: callback, interval");
        if (!JS_IsFunction(ctx, argv[0]))
            return JS_ThrowTypeError(ctx, "First argument must be a callback function");

        double interval_ms;
        if (JS_ToFloat64(ctx, &interval_ms, argv[1]) != 0)
            return JS_ThrowTypeError(ctx, "Second argument must be a number (interval in ms)");
        if (interval_ms < 10) interval_ms = 10;

        JSMod* mod = get_js_mod(ctx);
        if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
        if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Timer))
            return JS_ThrowInternalError(ctx, "Timer subsystem disabled by circuit breaker");

        int32_t timer_id = mod->add_timer(ctx, argv[0], interval_ms, true);
        return JS_NewInt32(ctx, timer_id);
    }

    JSValue js_clear_timeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1) return JS_UNDEFINED;

        int32_t timer_id;
        if (JS_ToInt32(ctx, &timer_id, argv[0]) != 0)
            return JS_UNDEFINED;

        JSMod* mod = get_js_mod(ctx);
        if (mod) mod->cancel_timer(timer_id);

        return JS_UNDEFINED;
    }

    JSValue js_clear_interval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        return js_clear_timeout(ctx, this_val, argc, argv);
    }

} // namespace RC::JSScript
