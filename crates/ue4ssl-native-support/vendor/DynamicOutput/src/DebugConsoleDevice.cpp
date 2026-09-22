#include <DynamicOutput/DebugConsoleDevice.hpp>
#include <Compat/RustSupportFFI.hpp>

namespace RC::Output
{
    DebugConsoleDevice::DebugConsoleDevice() : m_sink(ue4ssl_native_log_console_new())
    {
        if (!m_sink)
        {
            THROW_INTERNAL_OUTPUT_ERROR("[DebugConsoleDevice] Failed to create console device")
        }
    }
    DebugConsoleDevice::~DebugConsoleDevice()
    {
        if (!ue4ssl_native_log_sink_close(m_sink)) { Internal::internal_error = true; }
    }
    auto DebugConsoleDevice::has_optional_arg() const -> bool { return true; }
    auto DebugConsoleDevice::receive(RC::StringViewType content) const -> void { receive_with_optional_arg(content, Color::NoColor); }
    auto DebugConsoleDevice::receive_with_optional_arg(RC::StringViewType content, int32_t level) const -> void
    {
        const auto formatted = m_formatter(content);
        if (!ue4ssl_native_log_console_write(m_sink, reinterpret_cast<const uint16_t*>(formatted.data()), formatted.size(), level))
        {
            THROW_INTERNAL_OUTPUT_ERROR("[DebugConsoleDevice::receive] Failed to write console output")
        }
    }
}
