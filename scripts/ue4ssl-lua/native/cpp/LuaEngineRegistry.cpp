#include "LuaEngineRegistry.hpp"

#include <algorithm>
#include <utility>

#include <Mod/LuaMod.hpp>

namespace RC::LuaEngineRegistry
{
    static std::mutex s_lua_mods_mutex{};
    static std::vector<std::unique_ptr<LuaMod>> s_lua_mods{};

    auto get_mutex() -> std::mutex&
    {
        return s_lua_mods_mutex;
    }

    auto get_mods() -> std::vector<std::unique_ptr<LuaMod>>&
    {
        return s_lua_mods;
    }

    auto add_mod(std::unique_ptr<LuaMod> mod) -> LuaMod*
    {
        std::lock_guard<std::mutex> lock{s_lua_mods_mutex};
        auto* mod_ptr = mod.get();
        s_lua_mods.emplace_back(std::move(mod));
        return mod_ptr;
    }

    auto clear() -> void
    {
        std::vector<std::unique_ptr<LuaMod>> mods_to_clear{};
        {
            std::lock_guard<std::mutex> lock{s_lua_mods_mutex};
            mods_to_clear.swap(s_lua_mods);
        }

        for (auto it = mods_to_clear.rbegin(); it != mods_to_clear.rend(); ++it)
        {
            auto& mod = *it;
            if (mod && mod->is_started())
            {
                mod->uninstall();
            }
        }
    }

    auto find_mod_by_name(StringViewType mod_name, UE4SSProgram::IsInstalled installed_only, UE4SSProgram::IsStarted started_only) -> LuaMod*
    {
        std::lock_guard<std::mutex> lock{s_lua_mods_mutex};
        for (auto& mod : s_lua_mods)
        {
            if (!mod || mod->get_name() != mod_name)
            {
                continue;
            }

            if (installed_only == UE4SSProgram::IsInstalled::Yes && !mod->is_installed())
            {
                continue;
            }

            if (started_only == UE4SSProgram::IsStarted::Yes && !mod->is_started())
            {
                continue;
            }

            return mod.get();
        }

        return nullptr;
    }

    template <typename Predicate>
    static auto take_mod_if(Predicate&& predicate) -> std::unique_ptr<LuaMod>
    {
        std::lock_guard<std::mutex> lock{s_lua_mods_mutex};
        auto it = std::find_if(s_lua_mods.begin(), s_lua_mods.end(), std::forward<Predicate>(predicate));
        if (it == s_lua_mods.end())
        {
            return nullptr;
        }

        auto mod = std::move(*it);
        s_lua_mods.erase(it);
        return mod;
    }

    auto take_mod_by_name(StringViewType mod_name) -> std::unique_ptr<LuaMod>
    {
        return take_mod_if([&](const auto& mod) {
            return mod && mod->get_name() == mod_name;
        });
    }
} // namespace RC::LuaEngineRegistry
