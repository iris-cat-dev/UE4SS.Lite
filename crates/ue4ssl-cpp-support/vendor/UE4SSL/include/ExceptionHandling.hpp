#pragma once

#include <stdexcept>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>

#define UE4SS_ERROR_OUTPUTTER()                                                                                                                                \
    if (!Output::has_internal_error())                                                                                                                         \
    {                                                                                                                                                          \
        Output::send<LogLevel::Error>(STR("Error: {}\n"), ensure_str(e.what()));                                                                                    \
    }                                                                                                                                                          \
    else                                                                                                                                                       \
    {                                                                                                                                                          \
        printf_s("Internal Error: %s\n", e.what());                                                                                                            \
    }

namespace RC
{
    // Will try some code and properly propagate any exceptions
    // This is a simple helper function to avoid having 15 extra lines of code everywhere
    template <typename CodeToTry>
    auto constexpr TRY(CodeToTry code_to_try)
    {
        try
        {
            return code_to_try();
        }
        catch (std::exception& e)
        {
            UE4SS_ERROR_OUTPUTTER()

            using LambdaReturnType = decltype(code_to_try());
            if constexpr (!std::is_same_v<LambdaReturnType, void>)
            {
                return LambdaReturnType{};
            }
        }
        catch (...)
        {
            if (!Output::has_internal_error())
            {
                Output::send<LogLevel::Error>(STR("Unknown non-standard exception caught\n"));
            }
            else
            {
                printf_s("Unknown non-standard exception caught\n");
            }

            using LambdaReturnType = decltype(code_to_try());
            if constexpr (!std::is_same_v<LambdaReturnType, void>)
            {
                return LambdaReturnType{};
            }
        }
    }

    // Overload with subsystem identifier for better diagnostics
    template <typename CodeToTry>
    auto constexpr TRY(const wchar_t* subsystem, CodeToTry code_to_try)
    {
        try
        {
            return code_to_try();
        }
        catch (std::exception& e)
        {
            if (!Output::has_internal_error())
            {
                Output::send<LogLevel::Error>(STR("[{}] Error: {}\n"), subsystem, ensure_str(e.what()));
            }
            else
            {
                printf_s("[%ls] Internal Error: %s\n", subsystem, e.what());
            }

            using LambdaReturnType = decltype(code_to_try());
            if constexpr (!std::is_same_v<LambdaReturnType, void>)
            {
                return LambdaReturnType{};
            }
        }
        catch (...)
        {
            if (!Output::has_internal_error())
            {
                Output::send<LogLevel::Error>(STR("[{}] Unknown non-standard exception caught\n"), subsystem);
            }
            else
            {
                printf_s("[%ls] Unknown non-standard exception caught\n", subsystem);
            }

            using LambdaReturnType = decltype(code_to_try());
            if constexpr (!std::is_same_v<LambdaReturnType, void>)
            {
                return LambdaReturnType{};
            }
        }
    }
} // namespace RC
