#ifndef IO_INPUT_HANDLER_HPP
#define IO_INPUT_HANDLER_HPP

// TODO: Abstract more... need to get rid of Windows.h from InputHandler.cpp

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <Input/Common.hpp>
#include <Input/KeyDef.hpp>

namespace RC::Input
{
    using EventCallbackCallable = std::function<void()>;

    extern "C"
    {
        auto ue4ssl_native_input_handler_new() -> void*;
        auto ue4ssl_native_input_handler_destroy(void*) -> void;
        auto ue4ssl_native_input_handler_add_window_class(void*, const uint16_t*) -> void;
    }

    auto is_modifier_key_required(ModifierKey, std::vector<ModifierKey>) -> bool;

    struct KeyData
    {
        std::vector<ModifierKey> required_modifier_keys{};
        std::vector<EventCallbackCallable> callbacks{};
        uint8_t custom_data{};
        void* custom_data2{};
        bool requires_modifier_keys{};
        bool is_down{};
    };

    struct KeySet
    {
        std::unordered_map<Key, std::vector<KeyData>> key_data;
    };

    class RC_INPUT_API Handler
    {
      private:
        std::vector<const wchar_t*> m_active_window_classes{};
        std::vector<KeySet> m_key_sets{};
        std::vector<std::unique_ptr<EventCallbackCallable>> m_native_callbacks{};
        std::unordered_map<ModifierKey, bool> m_modifier_keys_down{};
        void* m_native_handler{};
        bool m_any_keys_are_down{};
        bool m_allow_input{true};

      public:
        Handler() = delete;
        template <typename... WindowClasses>
        explicit Handler(WindowClasses... window_classes)
        {
            static_assert(std::conjunction<std::is_same<const wchar_t*, WindowClasses>...>::value, "WindowClasses must be of type const wchar_t*");

            m_native_handler = ue4ssl_native_input_handler_new();
            m_modifier_keys_down.emplace(ModifierKey::SHIFT, false);
            m_modifier_keys_down.emplace(ModifierKey::CONTROL, false);
            m_modifier_keys_down.emplace(ModifierKey::ALT, false);

            register_window_classes(window_classes...);
        }

        ~Handler();

      private:
        template <typename WindowClass>
        auto register_window_classes(WindowClass window_class) -> void
        {
            m_active_window_classes.emplace_back(window_class);
            ue4ssl_native_input_handler_add_window_class(m_native_handler, reinterpret_cast<const uint16_t*>(window_class));
        }

        template <typename WindowClass, typename... WindowClasses>
        auto register_window_classes(WindowClass window_class, WindowClasses... window_classes) -> void
        {
            m_active_window_classes.emplace_back(window_class);
            ue4ssl_native_input_handler_add_window_class(m_native_handler, reinterpret_cast<const uint16_t*>(window_class));
            register_window_classes(window_classes...);
        }

        auto are_modifier_keys_down(const std::vector<ModifierKey>&) -> bool;
        auto is_program_focused() -> bool;

      public:
        auto process_event() -> void;
        auto register_keydown_event(Input::Key, EventCallbackCallable, uint8_t custom_data = 0, void* custom_data2 = nullptr) -> void;

        using ModifierKeyArray = std::array<Input::ModifierKey, max_modifier_keys>;
        auto register_keydown_event(Input::Key, const ModifierKeyArray&, const EventCallbackCallable&, uint8_t custom_data = 0, void* custom_data2 = nullptr)
                -> void;

        auto is_keydown_event_registered(Input::Key) -> bool;
        auto is_keydown_event_registered(Input::Key, const ModifierKeyArray&) -> bool;

        auto get_events() -> std::vector<KeySet>&;
        auto get_allow_input() -> bool;
        auto set_allow_input(bool new_value) -> void;
    };
} // namespace RC::Input

#endif // IO_INPUT_HANDLER_HPP
