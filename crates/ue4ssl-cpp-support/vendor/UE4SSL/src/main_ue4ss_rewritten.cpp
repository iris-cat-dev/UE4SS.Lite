#define NOMINMAX
#include <Windows.h>

#include <Compat/BootstrapShim.hpp>

#define WIN_API_FUNCTION_NAME DllMain

auto WIN_API_FUNCTION_NAME(HMODULE hModule, DWORD ul_reason_for_call, [[maybe_unused]] LPVOID lpReserved) -> BOOL
{
    __try
    {
        switch (ul_reason_for_call)
        {
        case DLL_PROCESS_ATTACH:
            RC::Compat::Bootstrap::dll_process_attached(hModule);
            break;
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            RC::Compat::Bootstrap::static_cleanup_seh();
            break;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return TRUE;
}
