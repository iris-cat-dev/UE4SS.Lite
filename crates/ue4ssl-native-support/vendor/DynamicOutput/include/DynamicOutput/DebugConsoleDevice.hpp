#ifndef UE4SS_REWRITTEN_DEBUGCONSOLEDEVICE_HPP
#define UE4SS_REWRITTEN_DEBUGCONSOLEDEVICE_HPP

#include <cstdint>
#include <DynamicOutput/Common.hpp>
#include <DynamicOutput/OutputDevice.hpp>

namespace RC::Output
{
    class RC_DYNOUT_API DebugConsoleDevice : public OutputDevice
    {
        uint64_t m_sink{};
      public:
        DebugConsoleDevice();
        DebugConsoleDevice(const DebugConsoleDevice&) = delete;
        auto operator=(const DebugConsoleDevice&) -> DebugConsoleDevice& = delete;
        ~DebugConsoleDevice() override;
        auto has_optional_arg() const -> bool override;
        auto receive(RC::StringViewType content) const -> void override;
        auto receive_with_optional_arg(RC::StringViewType content, int32_t level = 0) const -> void override;
    };
}

#endif // UE4SS_REWRITTEN_DEBUGCONSOLEDEVICE_HPP
