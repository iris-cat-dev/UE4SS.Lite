#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <Input/Handler.hpp>

extern "C"
{
    auto ue4ssl_native_input_is_key_down(int32_t key) -> uint8_t;
    auto ue4ssl_native_input_foreground_class_matches(const uint16_t* class_name) -> uint8_t;
    auto ue4ssl_native_input_handler_register_keydown_event(
            void* handler,
            uint8_t key,
            const uint8_t* modifier_keys,
            size_t modifier_key_count,
            void (*callback)(void*),
            void* callback_data) -> void;
    auto ue4ssl_native_input_handler_process_event(void* handler) -> void;
    auto ue4ssl_native_input_handler_get_allow_input(void* handler) -> uint8_t;
    auto ue4ssl_native_input_handler_set_allow_input(void* handler, uint8_t allow_input) -> void;
}

namespace
{
    auto native_callback_trampoline(void* callback_data) -> void
    {
        auto* callback = static_cast<RC::Input::EventCallbackCallable*>(callback_data);
        if (callback)
        {
            (*callback)();
        }
    }
}

namespace RC::Input
{
    auto static is_key_down(int32_t key) -> bool
    {
        return ue4ssl_native_input_is_key_down(key) != 0;
    }

    auto is_modifier_key_required(ModifierKey modifier_key, std::vector<ModifierKey> modifier_keys) -> bool
    {
        for (const auto& required_modifier_key : modifier_keys)
        {
            if (required_modifier_key == ModifierKey::MOD_KEY_START_OF_ENUM)
            {
                continue;
            }

            if (modifier_key == required_modifier_key)
            {
                return true;
            }
        }

        return false;
    }

    Handler::~Handler()
    {
        ue4ssl_native_input_handler_destroy(m_native_handler);
        m_native_handler = nullptr;
    }

    auto Handler::are_modifier_keys_down(const std::vector<ModifierKey>& required_modifier_keys) -> bool
    {
        bool are_required_modifier_keys_down = true;

        for (const auto& [modifier_key, modifier_key_is_down] : m_modifier_keys_down)
        {
            for (const auto& required_modifier_key : required_modifier_keys)
            {
                if (modifier_key == required_modifier_key && !modifier_key_is_down)
                {
                    are_required_modifier_keys_down = false;
                }

                if (modifier_key != required_modifier_key && modifier_key_is_down && !is_modifier_key_required(modifier_key, required_modifier_keys))
                {
                    return false;
                }
            }
        }

        return are_required_modifier_keys_down;
    }

    auto Handler::is_program_focused() -> bool
    {
        for (const auto& active_window_class : m_active_window_classes)
        {
            if (ue4ssl_native_input_foreground_class_matches(reinterpret_cast<const uint16_t*>(active_window_class)) != 0)
            {
                return true;
            }
        }

        return false;
    }

    auto Handler::process_event() -> void
    {
        ue4ssl_native_input_handler_process_event(m_native_handler);
    }

    auto Handler::register_keydown_event(Input::Key key, EventCallbackCallable callback, uint8_t custom_data, void* custom_data2) -> void
    {
        KeySet& key_set = [&]() -> KeySet& {
            for (auto& key_set : m_key_sets)
            {
                if (key_set.key_data.contains(key))
                {
                    return key_set;
                }
            }

            return m_key_sets.emplace_back(KeySet{});
        }();

        KeyData& key_data = key_set.key_data[key].emplace_back();
        auto native_callback = std::make_unique<EventCallbackCallable>(callback);
        auto* native_callback_ptr = native_callback.get();
        m_native_callbacks.emplace_back(std::move(native_callback));
        key_data.callbacks.emplace_back(callback);
        key_data.custom_data = custom_data;
        key_data.custom_data2 = custom_data2;

        ue4ssl_native_input_handler_register_keydown_event(
                m_native_handler,
                static_cast<uint8_t>(key),
                nullptr,
                0,
                native_callback_trampoline,
                native_callback_ptr);
    }

    auto Handler::register_keydown_event(
            Input::Key key, const ModifierKeyArray& modifier_keys, const EventCallbackCallable& callback, uint8_t custom_data, void* custom_data2) -> void
    {
        KeySet& key_set = [&]() -> KeySet& {
            for (auto& key_set : m_key_sets)
            {
                if (key_set.key_data.contains(key))
                {
                    return key_set;
                }
            }

            return m_key_sets.emplace_back(KeySet{});
        }();

        KeyData& key_data = key_set.key_data[key].emplace_back();
        auto native_callback = std::make_unique<EventCallbackCallable>(callback);
        auto* native_callback_ptr = native_callback.get();
        m_native_callbacks.emplace_back(std::move(native_callback));
        key_data.callbacks.emplace_back(callback);
        key_data.custom_data = custom_data;
        key_data.custom_data2 = custom_data2;
        key_data.requires_modifier_keys = true;

        for (const auto& modifier_key : modifier_keys)
        {
            if (modifier_key != ModifierKey::MOD_KEY_START_OF_ENUM)
            {
                key_data.required_modifier_keys.emplace_back(modifier_key);
            }
        }

        ue4ssl_native_input_handler_register_keydown_event(
                m_native_handler,
                static_cast<uint8_t>(key),
                reinterpret_cast<const uint8_t*>(modifier_keys.data()),
                modifier_keys.size(),
                native_callback_trampoline,
                native_callback_ptr);
    }

    auto Handler::is_keydown_event_registered(Input::Key key) -> bool
    {
        bool is_key_registered{};
        bool is_key_registered_with_no_modifier_keys = true;
        for (const auto& key_set : m_key_sets)
        {
            for (const auto& [key_registered, key_data_container] : key_set.key_data)
            {
                if (key_registered == key)
                {
                    is_key_registered = true;
                }
                else
                {
                    continue;
                }

                for (const auto& key_data : key_data_container)
                {
                    if (key_data.requires_modifier_keys)
                    {
                        is_key_registered_with_no_modifier_keys = false;
                        break;
                    }
                }

                if (!is_key_registered_with_no_modifier_keys)
                {
                    break;
                }
            }

            if (!is_key_registered_with_no_modifier_keys)
            {
                break;
            }
        }

        return is_key_registered && is_key_registered_with_no_modifier_keys;
    }

    auto Handler::is_keydown_event_registered(Input::Key key, const ModifierKeyArray& modifier_keys) -> bool
    {
        bool is_key_registered{};
        bool all_modifier_keys_match{};
        for (const auto& key_set : m_key_sets)
        {
            for (const auto& [key_registered, key_data_container] : key_set.key_data)
            {
                if (key_registered == key)
                {
                    is_key_registered = true;
                }
                else
                {
                    continue;
                }

                all_modifier_keys_match = false;
                for (const auto& key_data : key_data_container)
                {
                    if (!key_data.requires_modifier_keys && !modifier_keys.empty())
                    {
                        all_modifier_keys_match = false;
                        continue;
                    }

                    if (!key_data.requires_modifier_keys && modifier_keys.empty())
                    {
                        all_modifier_keys_match = true;
                        break;
                    }

                    for (const auto& modifier_key : key_data.required_modifier_keys)
                    {
                        for (const auto modifier_key_to_check : modifier_keys)
                        {
                            if (modifier_key_to_check == ModifierKey::MOD_KEY_START_OF_ENUM)
                            {
                                continue;
                            }

                            if (modifier_key_to_check == modifier_key)
                            {
                                all_modifier_keys_match = true;
                            }
                            else
                            {
                                all_modifier_keys_match = false;
                            }
                        }

                        if (all_modifier_keys_match)
                        {
                            break;
                        }
                    }

                    if (all_modifier_keys_match)
                    {
                        break;
                    }
                }

                if (all_modifier_keys_match)
                {
                    break;
                }
            }

            if (all_modifier_keys_match)
            {
                break;
            }
        }

        return is_key_registered && all_modifier_keys_match;
    }

    auto Handler::get_events() -> std::vector<KeySet>&
    {
        return m_key_sets;
    }

    auto Handler::get_allow_input() -> bool
    {
        m_allow_input = ue4ssl_native_input_handler_get_allow_input(m_native_handler) != 0;
        return m_allow_input;
    }

    auto Handler::set_allow_input(bool new_value) -> void
    {
        m_allow_input = new_value;
        ue4ssl_native_input_handler_set_allow_input(m_native_handler, static_cast<uint8_t>(new_value));
    }
} // namespace RC::Input
