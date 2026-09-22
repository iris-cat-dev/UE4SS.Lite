#include <exception>
#include <memory>
#include <utility>
#include <Input/Handler.hpp>
#include <DynamicOutput/Macros.hpp>
#if defined(_WIN32) && defined(_MSC_VER)
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
    thread_local std::exception_ptr* callback_error{};

    // These leaves own no C++ temporaries; SEH is contained before control returns to Rust.
    auto invoke_callback(void* data, bool release) -> bool
    {
#if defined(_WIN32) && defined(_MSC_VER)
        __try
        {
#endif
            if (release) { delete static_cast<RC::Input::EventCallbackCallable*>(data); }
            else { (*static_cast<RC::Input::EventCallbackCallable*>(data))(); }
            return true;
#if defined(_WIN32) && defined(_MSC_VER)
        }
        __except (GetExceptionCode() == 0xE06D7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
#endif
    }

    auto native_callback_trampoline(void* data) noexcept -> void
    {
        if (callback_error && *callback_error)
        {
            return;
        }
        try
        {
            if (!invoke_callback(data, false)) { throw std::runtime_error{"Structured exception in input callback"}; }
        }
        catch (...)
        {
            RC::Output::Internal::internal_error = true;
            if (callback_error)
            {
                *callback_error = std::current_exception();
            }
        }
    }

    auto native_callback_release(void* data) noexcept -> void
    {
        try
        {
            if (!invoke_callback(data, true))
            {
                RC::Output::Internal::internal_error = true;
                if (callback_error) { *callback_error = std::make_exception_ptr(std::runtime_error{"Structured exception releasing input callback"}); }
            }
        }
        catch (...)
        {
            RC::Output::Internal::internal_error = true;
            if (callback_error) { *callback_error = std::current_exception(); }
        }
    }
}

namespace RC::Input
{
    Handler::~Handler()
    {
        ue4ssl_native_input_handler_destroy(m_native_handler);
    }

    auto Handler::process_event() -> void
    {
        std::exception_ptr error;
        auto* previous = std::exchange(callback_error, &error);
        ue4ssl_native_input_handler_process_event(m_native_handler);
        callback_error = previous;
        if (error)
        {
            std::rethrow_exception(error);
        }
    }

    auto Handler::register_keydown_event(Input::Key key, EventCallbackCallable callback, uintptr_t owner, uint8_t kind) -> EventHandle
    {
        return register_keydown_event(key, ModifierKeyArray{}, callback, owner, kind);
    }

    auto Handler::register_keydown_event(Input::Key key, const ModifierKeyArray& modifiers, const EventCallbackCallable& callback, uintptr_t owner, uint8_t kind) -> EventHandle
    {
        auto context = std::make_unique<EventCallbackCallable>(callback);
        const auto handle = ue4ssl_native_input_handler_register_keydown_event_v2(
                m_native_handler, static_cast<uint8_t>(key), reinterpret_cast<const uint8_t*>(modifiers.data()), modifiers.size(),
                owner, kind, native_callback_trampoline, context.get(), native_callback_release);
        if (!handle)
        {
            throw std::runtime_error{"Unable to register input callback"};
        }
        context.release();
        return handle;
    }

    auto Handler::unregister_event(EventHandle handle) -> bool
    {
        return ue4ssl_native_input_handler_unregister_event(m_native_handler, handle) != 0;
    }

    auto Handler::unregister_owner(uintptr_t owner) -> size_t
    {
        return ue4ssl_native_input_handler_unregister_owner(m_native_handler, owner);
    }

    auto Handler::unregister_kind(uint8_t kind) -> size_t
    {
        return ue4ssl_native_input_handler_unregister_kind(m_native_handler, kind);
    }

    auto Handler::is_keydown_event_registered(Input::Key key) -> bool
    {
        return is_keydown_event_registered(key, ModifierKeyArray{});
    }

    auto Handler::is_keydown_event_registered(Input::Key key, const ModifierKeyArray& modifiers) -> bool
    {
        return ue4ssl_native_input_handler_is_keydown_event_registered(m_native_handler, static_cast<uint8_t>(key),
                reinterpret_cast<const uint8_t*>(modifiers.data()), modifiers.size()) != 0;
    }

    auto Handler::get_allow_input() -> bool
    {
        return ue4ssl_native_input_handler_get_allow_input(m_native_handler) != 0;
    }

    auto Handler::set_allow_input(bool allow) -> void
    {
        ue4ssl_native_input_handler_set_allow_input(m_native_handler, static_cast<uint8_t>(allow));
    }
} // namespace RC::Input
