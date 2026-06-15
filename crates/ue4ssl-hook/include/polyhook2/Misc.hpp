#pragma once

#include <cstdint>

namespace PLH
{
    template <typename FnCastTo>
    auto FnCast(uint64_t fn_to_cast, FnCastTo) -> FnCastTo
    {
        return reinterpret_cast<FnCastTo>(fn_to_cast);
    }

    template <typename FnCastTo>
    auto FnCast(void* fn_to_cast, FnCastTo) -> FnCastTo
    {
        return reinterpret_cast<FnCastTo>(fn_to_cast);
    }
}
