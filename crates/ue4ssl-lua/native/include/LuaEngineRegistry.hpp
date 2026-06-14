#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <UE4SSProgram.hpp>

namespace RC
{
    class LuaMod;

    namespace LuaEngineRegistry
    {
        auto get_mutex() -> std::mutex&;
        auto get_mods() -> std::vector<std::unique_ptr<LuaMod>>&;
        auto add_mod(std::unique_ptr<LuaMod> mod) -> LuaMod*;
        auto clear() -> void;
        auto find_mod_by_name(StringViewType mod_name, UE4SSProgram::IsInstalled installed_only, UE4SSProgram::IsStarted started_only) -> LuaMod*;
        auto take_mod_by_name(StringViewType mod_name) -> std::unique_ptr<LuaMod>;
    } // namespace LuaEngineRegistry
} // namespace RC
