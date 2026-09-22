#ifndef IO_INPUT_HANDLER_HPP
#define IO_INPUT_HANDLER_HPP

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <Input/Common.hpp>
#include <Input/KeyDef.hpp>
#include <Compat/RustSupportFFI.hpp>

namespace RC::Input
{
    using EventCallbackCallable = std::function<void()>;
    using EventHandle = uint64_t;

    class RC_INPUT_API Handler
    {
      private:
        void* m_native_handler{};

      public:
        Handler() = delete;
        template <typename... WindowClasses>
        explicit Handler(WindowClasses... window_classes)
        {
            static_assert(std::conjunction<std::is_same<const wchar_t*, WindowClasses>...>::value, "WindowClasses must be of type const wchar_t*");
            m_native_handler = ue4ssl_native_input_handler_new();
            if (!m_native_handler)
            {
                throw std::runtime_error{"Unable to create input handler"};
            }
            (ue4ssl_native_input_handler_add_window_class(m_native_handler, reinterpret_cast<const uint16_t*>(window_classes)), ...);
        }
        Handler(const Handler&) = delete;
        auto operator=(const Handler&) -> Handler& = delete;
        ~Handler();

        using ModifierKeyArray = std::array<Input::ModifierKey, max_modifier_keys>;
        auto process_event() -> void;
        auto register_keydown_event(Input::Key, EventCallbackCallable, uintptr_t owner = 0, uint8_t kind = 0) -> EventHandle;
        auto register_keydown_event(Input::Key, const ModifierKeyArray&, const EventCallbackCallable&, uintptr_t owner = 0, uint8_t kind = 0) -> EventHandle;
        auto unregister_event(EventHandle) -> bool;
        auto unregister_owner(uintptr_t owner) -> size_t;
        auto unregister_kind(uint8_t kind) -> size_t;
        auto is_keydown_event_registered(Input::Key) -> bool;
        auto is_keydown_event_registered(Input::Key, const ModifierKeyArray&) -> bool;
        auto get_allow_input() -> bool;
        auto set_allow_input(bool new_value) -> void;
    };
} // namespace RC::Input

#endif // IO_INPUT_HANDLER_HPP
