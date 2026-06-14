#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <String/StringType.hpp>

namespace RC::Compat
{
    inline constexpr std::string_view kCppModStartExport{"start_mod"};
    inline constexpr std::string_view kCppModUninstallExport{"uninstall_mod"};

    enum class ScriptKeybindCustomData : uint8_t
    {
        Lua = 1,
        Cpp = 2,
        JavaScript = 3,
    };

    struct BuiltinModContract
    {
        StringViewType mod_name{};
        StringViewType dll_name{};
    };

    inline constexpr std::array<BuiltinModContract, 3> kBuiltinModLoadOrder{
            BuiltinModContract{STR("UE4SSL.JavaScript"), STR("UE4SSL.JavaScript.dll")},
            BuiltinModContract{STR("UE4SSL.Lua"), STR("UE4SSL.Lua.dll")}
    };
} // namespace RC::Compat
