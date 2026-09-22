#define NOMINMAX

#include <Compat/RustRuntimeFFI.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppMod.hpp>
#include <UE4SSProgram.hpp>

namespace
{
    auto make_slice(RC::StringViewType value) -> RC::Compat::RustCore::SliceU16
    {
        return {reinterpret_cast<const uint16_t*>(value.data()), value.size()};
    }

    auto copy_string(RC::Compat::RustCore::SliceU16 value) -> RC::StringType
    {
        if (!value.data || !value.len) return {};
        return {reinterpret_cast<const RC::CharType*>(value.data), value.len};
    }
}

namespace RC
{
    CppMod::CppMod(UE4SSProgram& program, uint64_t id, StringType&& name, StringType&& path)
        : Mod(program, std::move(name), std::filesystem::path{std::move(path)}), m_id(id)
    {
    }

    auto CppMod::set_installable(bool value) -> void { ue4ssl_runtime_mod_action(m_id, 1, value); }
    auto CppMod::is_installable() const -> bool { return ue4ssl_runtime_mod_status(m_id).installable != 0; }
    auto CppMod::set_installed(bool value) -> void { ue4ssl_runtime_mod_action(m_id, 2, value); }
    auto CppMod::is_installed() const -> bool { return ue4ssl_runtime_mod_status(m_id).installed != 0; }
    auto CppMod::is_started() const -> bool { return ue4ssl_runtime_mod_status(m_id).started != 0; }
    auto CppMod::set_updates_disabled(bool value) -> void { ue4ssl_runtime_mod_action(m_id, 3, value); }
    auto CppMod::are_updates_disabled() const -> bool { return ue4ssl_runtime_mod_status(m_id).updates_disabled != 0; }
    auto CppMod::start_mod() -> void { ue4ssl_runtime_mod_action(m_id, 4, 0); }
    auto CppMod::uninstall() -> void { ue4ssl_runtime_mod_action(m_id, 5, 0); }
    auto CppMod::fire_unreal_init() -> void { ue4ssl_runtime_mod_action(m_id, 6, 0); }
    auto CppMod::fire_ui_init() -> void { ue4ssl_runtime_mod_action(m_id, 7, 0); }
    auto CppMod::fire_program_start() -> void { ue4ssl_runtime_mod_action(m_id, 8, 0); }
    auto CppMod::fire_update() -> void { ue4ssl_runtime_mod_action(m_id, 9, 0); }
    auto CppMod::fire_dll_load(StringViewType name) -> void { ue4ssl_runtime_mod_dll_load(m_id, make_slice(name)); }
}

extern "C"
{
    auto ue4ssl_native_runtime_create_mod_view(uint64_t id,
                                              RC::Compat::RustCore::SliceU16 name,
                                              RC::Compat::RustCore::SliceU16 path) -> void*
    {
        try
        {
            return new RC::CppMod(RC::UE4SSProgram::get_program(), id, copy_string(name), copy_string(path));
        }
        catch (...) { return nullptr; }
    }

    auto ue4ssl_native_runtime_destroy_mod_view(void* view) -> void
    {
        try { delete static_cast<RC::CppMod*>(view); }
        catch (...) {}
    }

    auto ue4ssl_native_runtime_log(uint32_t level, RC::Compat::RustCore::SliceU16 message) -> void
    {
        try
        {
            const auto text = copy_string(message);
            if (level >= 4) RC::Output::send<RC::LogLevel::Error>(STR("{}"), text);
            else if (level == 3) RC::Output::send<RC::LogLevel::Warning>(STR("{}"), text);
            else RC::Output::send(STR("{}"), text);
        }
        catch (...) {}
    }
}
