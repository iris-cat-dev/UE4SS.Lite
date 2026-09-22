#include <Compat/RustRuntimeFFI.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <LuaCompat.hpp>
#include <Mod/LuaMod.hpp>
#include <UE4SSProgram.hpp>
#include <Unreal/UEngine.hpp>
#include <Unreal/UObject.hpp>

#include "GUI/Dumpers.hpp"
#include "LuaEngineRegistry.hpp"
#include "UE4SSRuntime.hpp"
#include "USMapGenerator/Generator.hpp"

namespace
{
    auto log_lite_compat_unavailable(RC::StringViewType feature_name) -> void
    {
        RC::Output::send<RC::LogLevel::Warning>(
                STR("[UE4SSL.Lua] '{}' is intentionally unavailable in UE4SS-Lite. ")
                STR("The extracted Lua engine keeps this API for compatibility, but the underlying dump/generator tooling is not shipped in Lite.\n"),
                feature_name);
    }

    auto log_lite_compat_unavailable(RC::StringViewType feature_name, RC::StringViewType requested_output) -> void
    {
        RC::Output::send<RC::LogLevel::Warning>(
                STR("[UE4SSL.Lua] '{}' is intentionally unavailable in UE4SS-Lite. ")
                STR("Requested output: {}. The extracted Lua engine keeps this API for compatibility, but the underlying dump/generator tooling is not shipped in Lite.\n"),
                feature_name,
                requested_output);
    }
} // namespace

namespace RC
{
    static auto recreate_lua_mod(UE4SSProgram& program, std::unique_ptr<LuaMod> old_mod) -> bool
    {
        if (!old_mod)
        {
            return false;
        }

        const auto mod_name = StringType{old_mod->get_name()};
        const auto mod_path = old_mod->get_scripts_path().parent_path();

        if (old_mod->is_started())
        {
            old_mod->uninstall();
        }

        auto new_mod = std::make_unique<LuaMod>(program, StringType{mod_name}, ensure_str(mod_path));
        if (!new_mod->is_installable())
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.Lua] Reloaded mod '{}' is no longer installable.\n"), mod_name);
            return false;
        }

        new_mod->set_installed(true);
        auto* new_mod_ptr = LuaEngineRegistry::add_mod(std::move(new_mod));
        new_mod_ptr->start_mod();

        Output::send(STR("Mod '{}' reinstalled\n"), mod_name);
        return true;
    }

    static auto uninstall_lua_mod(std::unique_ptr<LuaMod> mod) -> bool
    {
        if (!mod)
        {
            return false;
        }

        const auto mod_name = StringType{mod->get_name()};
        if (mod->is_started())
        {
            mod->uninstall();
        }

        Output::send(STR("Mod '{}' uninstalled\n"), mod_name);
        return true;
    }

    auto UE4SSRuntime::IsEngineTickAvailable() -> bool
    {
        return Unreal::UEngine::TickInternal.is_ready();
    }

    auto UE4SSRuntime::IsProcessEventAvailable() -> bool
    {
        return Unreal::UObject::ProcessEventInternal.is_ready();
    }
} // namespace RC

namespace RC::LuaCompat
{
    struct QueuedLuaModAction
    {
        std::string mod_name{};
        bool reinstall{};
    };

    static auto execute_queued_lua_mod_action(void* data) -> void
    {
        auto* action = static_cast<QueuedLuaModAction*>(data);
        if (!action)
        {
            return;
        }

        auto& program = UE4SSProgram::get_program();

        auto mod = LuaEngineRegistry::take_mod_by_name(ensure_str(action->mod_name));
        if (!mod)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.Lua] Could not process Lua mod '{}'.\n"), ensure_str(action->mod_name));
            return;
        }

        action->reinstall ? recreate_lua_mod(program, std::move(mod)) : uninstall_lua_mod(std::move(mod));
    }

    static auto release_queued_lua_mod_action(void* data) -> void
    {
        delete static_cast<QueuedLuaModAction*>(data);
    }

    auto find_mod_by_name(StringViewType mod_name, UE4SSProgram::IsInstalled installed_only, UE4SSProgram::IsStarted started_only) -> LuaMod*
    {
        return LuaEngineRegistry::find_mod_by_name(mod_name, installed_only, started_only);
    }

    auto find_mod_by_name(std::string_view mod_name, UE4SSProgram::IsInstalled installed_only, UE4SSProgram::IsStarted started_only) -> LuaMod*
    {
        return find_mod_by_name(ensure_str(mod_name), installed_only, started_only);
    }

    auto queue_reinstall_mod_by_name(std::string_view mod_name) -> void
    {
        if (mod_name.empty())
        {
            return;
        }

        UE4SSProgram::get_program().queue_event_owned(
                ue4ssl_runtime_current_owner(), execute_queued_lua_mod_action,
                new QueuedLuaModAction{std::string{mod_name}, true}, release_queued_lua_mod_action);
    }

    auto queue_reinstall_mod_by_name(StringViewType mod_name) -> void
    {
        auto mod_name_utf8 = to_string(mod_name);
        queue_reinstall_mod_by_name(std::string_view{mod_name_utf8});
    }

    auto queue_uninstall_mod_by_name(std::string_view mod_name) -> void
    {
        if (mod_name.empty())
        {
            return;
        }

        UE4SSProgram::get_program().queue_event_owned(
                ue4ssl_runtime_current_owner(), execute_queued_lua_mod_action,
                new QueuedLuaModAction{std::string{mod_name}, false}, release_queued_lua_mod_action);
    }

    auto queue_uninstall_mod_by_name(StringViewType mod_name) -> void
    {
        auto mod_name_utf8 = to_string(mod_name);
        queue_uninstall_mod_by_name(std::string_view{mod_name_utf8});
    }

    auto generate_cxx_headers(const RC::StringType& output_directory) -> void
    {
        log_lite_compat_unavailable(STR("GenerateSDK"), output_directory);
    }

    auto generate_lua_types(const RC::StringType& output_directory) -> void
    {
        log_lite_compat_unavailable(STR("GenerateLuaTypes"), output_directory);
    }

    auto generate_uht_compatible_headers() -> void
    {
        log_lite_compat_unavailable(STR("GenerateUHTCompatibleHeaders"));
    }

    auto dump_all_objects_and_properties(const RC::StringType& output_path_and_file_name) -> void
    {
        log_lite_compat_unavailable(STR("DumpAllObjects"), output_path_and_file_name);
    }
} // namespace RC::LuaCompat

namespace RC::GUI::Dumpers
{
    auto render() -> void
    {
    }

    auto call_generate_static_mesh_file() -> void
    {
        log_lite_compat_unavailable(STR("DumpStaticMeshes"));
    }

    auto call_generate_all_actor_file() -> void
    {
        log_lite_compat_unavailable(STR("DumpAllActors"));
    }

    auto call_generate_object_as_json([[maybe_unused]] Unreal::UObject* object) -> void
    {
        log_lite_compat_unavailable(STR("DumpObjectAsJSON"));
    }
} // namespace RC::GUI::Dumpers

namespace RC::OutTheShade
{
    auto generate_usmap() -> void
    {
        log_lite_compat_unavailable(STR("DumpUSMAP"));
    }
} // namespace RC::OutTheShade
