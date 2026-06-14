#include <filesystem>
#include <memory>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Mod/LuaMod.hpp>
#include <UE4SSProgram.hpp>

#include "LuaEngineRegistry.hpp"

using namespace RC;

class LuaScriptEngineMod : public CppUserModBase
{
private:
    bool m_program_start_ready{false};
    bool m_unreal_ready{false};
    bool m_discovered_mods{false};

private:
    static auto has_lua_entrypoint(const std::filesystem::path& mod_dir) -> bool
    {
        const auto script_path = mod_dir / "lua" / "main.lua";
        return std::filesystem::exists(script_path);
    }

    auto discover_and_start_lua_mods() -> void
    {
        auto& program = UE4SSProgram::get_program();
        const auto mods_dir = std::filesystem::path{program.get_mods_directory()};

        if (!std::filesystem::exists(mods_dir))
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.Lua] Mods directory does not exist: {}\n"), mods_dir.wstring());
            return;
        }

        size_t discovered_count{};
        for (const auto& entry : std::filesystem::directory_iterator(mods_dir))
        {
            std::error_code ec{};
            if (!entry.is_directory(ec) || ec)
            {
                continue;
            }

            const auto mod_dir = entry.path();
            const auto mod_name = ensure_str(mod_dir.stem());
            StringType mod_name_lower = mod_name;
            std::transform(mod_name_lower.begin(), mod_name_lower.end(), mod_name_lower.begin(), std::towlower);

            if (mod_name_lower == STR("shared"))
            {
                continue;
            }

            if (std::filesystem::exists(mod_dir / "main.dll"))
            {
                continue;
            }

            if (!has_lua_entrypoint(mod_dir))
            {
                continue;
            }

            auto lua_mod = std::make_unique<LuaMod>(program, ensure_str(mod_name), ensure_str(mod_dir));
            if (!lua_mod->is_installable())
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.Lua] Skipping non-installable Lua mod '{}'\n"), mod_name);
                continue;
            }

            lua_mod->set_installed(true);
            auto* lua_mod_ptr = LuaEngineRegistry::add_mod(std::move(lua_mod));
            lua_mod_ptr->start_mod();
            ++discovered_count;
        }

        Output::send<LogLevel::Normal>(STR("[UE4SSL.Lua] Started {} Lua mod(s)\n"), discovered_count);
    }

public:
    LuaScriptEngineMod()
    {
        ModName = STR("UE4SSL.Lua");
        ModVersion = STR("1.0.0");
        ModDescription = STR("Lua scripting support via extracted RE-UE4SS runtime");
        ModAuthors = STR("UE4SS Community");
    }

    ~LuaScriptEngineMod() override
    {
        LuaEngineRegistry::clear();
        LuaMod::global_uninstall();
    }

    auto on_program_start() -> void override
    {
        if (m_program_start_ready)
        {
            return;
        }

        LuaMod::on_program_start();
        m_program_start_ready = true;
        Output::send<LogLevel::Normal>(STR("[UE4SSL.Lua] Program start hooks initialized\n"));
    }

    auto on_unreal_init() -> void override
    {
        m_unreal_ready = true;
    }

    auto on_update() -> void override
    {
        if (m_discovered_mods || !m_program_start_ready || !m_unreal_ready)
        {
            return;
        }

        discover_and_start_lua_mods();
        m_discovered_mods = true;
    }
};

extern "C"
{
    __declspec(dllexport) CppUserModBase* start_mod()
    {
        Output::send<LogLevel::Normal>(STR("[UE4SSL.Lua] start_mod called\n"));
        return new LuaScriptEngineMod{};
    }

    __declspec(dllexport) void uninstall_mod(CppUserModBase* mod)
    {
        Output::send<LogLevel::Normal>(STR("[UE4SSL.Lua] uninstall_mod called\n"));
        delete mod;
    }
}
