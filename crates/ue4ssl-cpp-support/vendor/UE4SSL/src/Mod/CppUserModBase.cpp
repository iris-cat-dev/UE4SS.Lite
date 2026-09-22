#include <Mod/CppMod.hpp>
#include <Mod/CppUserModBase.hpp>
#include <format>
#include <Compat/CompatibilityContract.hpp>
#include <UE4SSProgram.hpp>
#include <String/StringType.hpp>

namespace RC
{
    CppUserModBase::CppUserModBase()
    {
        if (ModIntendedSDKVersion.empty())
        {
            ModIntendedSDKVersion = std::format(STR("{}.{}.{}"), UE4SS_LIB_VERSION_MAJOR, UE4SS_LIB_VERSION_MINOR, UE4SS_LIB_VERSION_HOTFIX);
        }
    }

    CppUserModBase::~CppUserModBase()
    {
        UE4SSProgram::get_program().unregister_input_owner(reinterpret_cast<uintptr_t>(this));
    }

    auto CppUserModBase::register_keydown_event(Input::Key key, const Input::EventCallbackCallable& callback, [[maybe_unused]] uint8_t custom_data) -> void
    {
        UE4SSProgram::get_program().register_keydown_event_owned(key, callback, static_cast<uint8_t>(Compat::ScriptKeybindCustomData::Cpp), reinterpret_cast<uintptr_t>(this));
    }

    auto CppUserModBase::register_keydown_event(Input::Key key,
                                                const Input::Handler::ModifierKeyArray& callback,
                                                const Input::EventCallbackCallable& modifier_keys,
                                                [[maybe_unused]] uint8_t custom_data) -> void
    {
        UE4SSProgram::get_program().register_keydown_event_owned(key, callback, modifier_keys, static_cast<uint8_t>(Compat::ScriptKeybindCustomData::Cpp), reinterpret_cast<uintptr_t>(this));
    }
} // namespace RC
