#pragma once

#include "PakSync/RuntimeTypes.hpp"

namespace RC::PakSync
{
    auto get_module_sections() -> ModuleSections;
    auto resolve_functions() -> FunctionTable;
    void log_resolve_result(const ResolveResult& result);
    void log_module_sections();
    auto all_required_resolved(const FunctionTable& table) -> bool;
}
