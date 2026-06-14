#pragma once

#include <Windows.h>

namespace RC
{
    class UE4SSProgram;
}

namespace RC::Compat::Bootstrap
{
    auto thread_dll_start(UE4SSProgram* program) -> unsigned long;
    auto process_initialized(HMODULE module_handle) -> void;
    auto dll_process_attached(HMODULE module_handle) -> void;
    auto static_cleanup_seh() -> void;
} // namespace RC::Compat::Bootstrap
