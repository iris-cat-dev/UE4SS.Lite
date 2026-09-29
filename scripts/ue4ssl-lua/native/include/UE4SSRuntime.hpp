#pragma once

#include <Common.hpp>

namespace RC
{
    struct UE4SSRuntime
    {
        static auto IsEngineTickAvailable() -> bool;
        static auto IsProcessEventAvailable() -> bool;
    };
} // namespace RC
